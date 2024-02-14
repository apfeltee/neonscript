
#if defined(_WIN32)
    #include <fcntl.h>
    #include <io.h>
#endif

#include "neon.h"

#if !defined(NEON_PLAT_ISWASM) && !defined(NEON_PLAT_ISWINDOWS)
    #define NEON_USE_LINENOISE
#endif

#if defined(NEON_USE_LINENOISE)
    #include "linenoise/utf8.h"
    #include "linenoise/linenoise.h"
#endif

static char* nn_cli_getinput(const char* prompt)
{
    #if !defined(NEON_USE_LINENOISE)
        enum { kMaxLineSize = 1024 };
        size_t len;
        char* rt;
        char rawline[kMaxLineSize+1] = {0};
        fprintf(stdout, "%s", prompt);
        fflush(stdout);
        rt = fgets(rawline, kMaxLineSize, stdin);
        len = strlen(rt);
        rt[len - 1] = 0;
        return rt;
    #else
        return linenoise(prompt);
    #endif
}

static void nn_cli_addhistoryline(const char* line)
{
    #if !defined(NEON_USE_LINENOISE)
        (void)line;
    #else
        linenoiseHistoryAdd(line);
    #endif
}

static void nn_cli_freeline(char* line)
{
    #if !defined(NEON_USE_LINENOISE)
        (void)line;
    #else
        linenoiseFree(line);
    #endif
}

#if !defined(NEON_PLAT_ISWASM)
static bool nn_cli_repl(NeonState* state)
{
    int i;
    int linelength;
    int bracecount;
    int parencount;
    int bracketcount;
    int doublequotecount;
    int singlequotecount;
    bool continuerepl;
    char* line;
    StringBuffer* source;
    const char* cursor;
    NeonValue dest;
    state->isrepl = true;
    continuerepl = true;
    printf("Type \".exit\" to quit or \".credits\" for more information\n");
    source = dyn_strbuf_makeempty(0);
    bracecount = 0;
    parencount = 0;
    bracketcount = 0;
    singlequotecount = 0;
    doublequotecount = 0;
    #if !defined(NEON_PLAT_ISWINDOWS)
        /* linenoiseSetEncodingFunctions(linenoiseUtf8PrevCharLen, linenoiseUtf8NextCharLen, linenoiseUtf8ReadCode); */
        linenoiseSetMultiLine(0);
        linenoiseHistoryAdd(".exit");
    #endif
    while(true)
    {
        if(!continuerepl)
        {
            bracecount = 0;
            parencount = 0;
            bracketcount = 0;
            singlequotecount = 0;
            doublequotecount = 0;
            dyn_strbuf_reset(source);
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
        line = nn_cli_getinput(cursor);
        fprintf(stderr, "line = %s. isexit=%d\n", line, strcmp(line, ".exit"));
        if(line == NULL || strcmp(line, ".exit") == 0)
        {
            dyn_strbuf_destroy(source);
            return true;
        }
        linelength = (int)strlen(line);
        if(strcmp(line, ".credits") == 0)
        {
            printf("\n" NEON_INFO_COPYRIGHT "\n\n");
            dyn_strbuf_reset(source);
            continue;
        }
        nn_cli_addhistoryline(line);
        if(linelength > 0 && line[0] == '#')
        {
            continue;
        }
        /* find count of { and }, ( and ), [ and ], " and ' */
        for(i = 0; i < linelength; i++)
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
        dyn_strbuf_appendstr(source, line);
        if(linelength > 0)
        {
            dyn_strbuf_appendstr(source, "\n");
        }
        nn_cli_freeline(line);
        if(bracketcount == 0 && parencount == 0 && bracecount == 0 && singlequotecount == 0 && doublequotecount == 0)
        {
            nn_state_execsource(state, state->topmodule, source->data, &dest);
            fflush(stdout);
            continuerepl = false;
        }
    }
    return true;
}
#endif

static bool nn_cli_runfile(NeonState* state, const char* file)
{
    size_t fsz;
    char* rp;
    char* source;
    const char* oldfile;
    NeonStatus result;
    source = nn_util_readfile(state, file, &fsz);
    if(source == NULL)
    {
        oldfile = file;
        source = nn_util_readfile(state, file, &fsz);
        if(source == NULL)
        {
            fprintf(stderr, "failed to read from '%s': %s\n", oldfile, strerror(errno));
            return false;
        }
    }
    state->rootphysfile = (char*)file;
    rp = osfn_realpath(file, NULL);
    state->topmodule->physicalpath = nn_string_copycstr(state, rp);
    nn_util_memfree(state, rp);
    result = nn_state_execsource(state, state->topmodule, source, NULL);
    nn_util_memfree(state, source);
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

static bool nn_cli_runcode(NeonState* state, char* source)
{
    NeonStatus result;
    state->rootphysfile = NULL;
    result = nn_state_execsource(state, state->topmodule, source, NULL);
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

int nn_util_findfirstpos(const char* str, int len, int ch)
{
    int i;
    for(i=0; i<len; i++)
    {
        if(str[i] == ch)
        {
            return i;
        }
    }
    return -1;
}

void nn_cli_parseenv(NeonState* state, char** envp)
{
    enum { kMaxKeyLen = 40 };
    int i;
    int len;
    int pos;
    char* raw;
    char* valbuf;
    char keybuf[kMaxKeyLen];
    NeonObjString* oskey;
    NeonObjString* osval;
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

void nn_cli_printtypesizes()
{
    #define ptyp(t) \
        { \
            fprintf(stdout, "%d\t%s\n", (int)sizeof(t), #t); \
            fflush(stdout); \
        }
    ptyp(NeonPrinter);
    ptyp(NeonValue);
    ptyp(NeonObject);
    ptyp(NeonPropGetSet);
    ptyp(NeonProperty);
    ptyp(NeonValArray);
    ptyp(NeonBlob);
    ptyp(NeonHashEntry);
    ptyp(NeonHashTable);
    ptyp(NeonObjString);
    ptyp(NeonObjUpvalue);
    ptyp(NeonObjModule);
    ptyp(NeonObjFuncScript);
    ptyp(NeonObjFuncClosure);
    ptyp(NeonObjClass);
    ptyp(NeonObjInstance);
    ptyp(NeonObjFuncBound);
    ptyp(NeonObjFuncNative);
    ptyp(NeonObjArray);
    ptyp(NeonObjRange);
    ptyp(NeonObjDict);
    ptyp(NeonObjFile);
    ptyp(NeonObjSwitch);
    ptyp(NeonObjUserdata);
    ptyp(NeonExceptionFrame);
    ptyp(NeonCallFrame);
    ptyp(NeonState);
    ptyp(NeonAstToken);
    ptyp(NeonAstLexer);
    ptyp(NeonAstLocal);
    ptyp(NeonAstUpvalue);
    ptyp(NeonAstFuncCompiler);
    ptyp(NeonAstClassCompiler);
    ptyp(NeonAstParser);
    ptyp(NeonAstRule);
    ptyp(NeonRegFunc);
    ptyp(NeonRegField);
    ptyp(NeonRegClass);
    ptyp(NeonRegModule);
    ptyp(NeonInstruction)
    #undef ptyp
}


void optprs_fprintmaybearg(FILE* out, const char* begin, const char* flagname, size_t flaglen, bool needval, bool maybeval, const char* delim)
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

void optprs_fprintusage(FILE* out, optlongflags_t* flags)
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

void nn_cli_showusage(char* argv[], optlongflags_t* flags, bool fail)
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
    NeonState* state;
    #if defined(NEON_PLAT_ISWINDOWS)
        _setmode(fileno(stdin), _O_BINARY);
        _setmode(fileno(stdout), _O_BINARY);
        _setmode(fileno(stderr), _O_BINARY);
    #endif
    ok = true;
    wasusage = false;
    quitafterinit = false;
    source = NULL;
    nextgcstart = NEON_CFG_DEFAULTGCSTART;
    state = nn_state_make();
    optlongflags_t longopts[] =
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
        {"astdebug", 'A', OPTPARSE_NONE, "print calls to the parser (very verbose, very slow)"},
        {"gcstart", 'g', OPTPARSE_REQUIRED, "set minimum bytes at which the GC should kick in. 0 disables GC"},
        {0, 0, (optargtype_t)0, NULL}
    };
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
        else if(co == 't')
        {
            nn_cli_printtypesizes();
            return 0;
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
        else if(co == 'A')
        {
            state->conf.enableastdebug = true;
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
        NeonObjString* os;
        state->cliargv = nn_object_makearray(state);
        for(i=0; i<nargc; i++)
        {
            os = nn_string_copycstr(state, nargv[i]);
            nn_array_push(state->cliargv, nn_value_fromobject(os));
        }
        nn_table_setcstr(state->globals, "ARGV", nn_value_fromobject(state->cliargv));
    }
    state->gcstate.nextgc = nextgcstart;
    nn_import_loadbuiltinmodules(state);
    if(source != NULL)
    {
        ok = nn_cli_runcode(state, source);
    }
    else if(nargc > 0)
    {
        filename = nn_value_asstring(state->cliargv->varray->values[0])->sbuf->data;
        fprintf(stderr, "nargv[0]=%s\n", filename);
        ok = nn_cli_runfile(state,  filename);
    }
    else
    {
        ok = nn_cli_repl(state);
    }
    cleanup:
    nn_state_destroy(state);
    if(ok)
    {
        return 0;
    }
    return 1;
}


