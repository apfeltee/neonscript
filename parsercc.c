
#include "neon.h"

/* how many switch cases per switch statement */
#define NEON_CONFIG_ASTMAXSWITCHCASES (32)

/* max number of function parameters */
#define NEON_CONFIG_ASTMAXFUNCPARAMS (32)

#define NN_ASTPARSER_GROWCAPACITY(capacity) \
    ((capacity) < 4 ? 4 : (capacity)*2)


static const char* g_strthis = "this";
static const char* g_strsuper = "super";

static void nn_astparser_runparser(NNAstParser* parser);
static void nn_astparser_ignorewhitespace(NNAstParser* prs);
static void nn_astparser_parsedeclaration(NNAstParser* prs);
static bool nn_astparser_raiseerroratv(NNAstParser* prs, NNAstToken* t, const char* message, va_list args);
static void nn_astparser_parseclassdeclaration(NNAstParser* prs, bool named);
static void nn_astparser_parsefuncdecl(NNAstParser* prs);
static void nn_astparser_advance(NNAstParser* prs);
static void nn_astparser_parsevardecl(NNAstParser* prs, bool isinitializer, bool isconst);
static void nn_astparser_parseexprstmt(NNAstParser* prs, bool isinitializer, bool semi);
static void nn_astparser_scopebegin(NNAstParser* prs);
static bool nn_astparser_check(NNAstParser* prs, NNAstTokType t);
static bool nn_astparser_parseblock(NNAstParser* prs);
static void nn_astparser_scopeend(NNAstParser* prs);
static void nn_astparser_parsestmt(NNAstParser* prs);
static void nn_astparser_synchronize(NNAstParser* prs);
static bool nn_astparser_consume(NNAstParser* prs, NNAstTokType t, const char* message);
static void nn_astparser_parseechostmt(NNAstParser* prs);
static void nn_astparser_parseifstmt(NNAstParser* prs);
static void nn_astparser_parsedo_whilestmt(NNAstParser* prs);
static void nn_astparser_parsewhilestmt(NNAstParser* prs);
static void nn_astparser_parseforstmt(NNAstParser* prs);
static void nn_astparser_parseforeachstmt(NNAstParser* prs);
static void nn_astparser_parseswitchstmt(NNAstParser* prs);
static void nn_astparser_parsecontinuestmt(NNAstParser* prs);
static void nn_astparser_parsebreakstmt(NNAstParser* prs);
static void nn_astparser_parsereturnstmt(NNAstParser* prs);
static void nn_astparser_parseassertstmt(NNAstParser* prs);
static void nn_astparser_parsethrowstmt(NNAstParser* prs);
static void nn_astparser_parsetrystmt(NNAstParser* prs);
static bool nn_astparser_rulebinary(NNAstParser* prs, NNAstToken previous, bool canassign);
static bool nn_astparser_parseprecedence(NNAstParser* prs, NNAstPrecedence precedence);
static int nn_astparser_parsevariable(NNAstParser* prs, const char* message);
static bool nn_astparser_rulecall(NNAstParser* prs, NNAstToken previous, bool canassign);
static uint8_t nn_astparser_parsefunccallargs(NNAstParser* prs);
static void nn_astparser_parseassign(NNAstParser* prs, uint8_t realop, uint8_t getop, uint8_t setop, int arg);
static bool nn_astparser_parseexpression(NNAstParser* prs);
static bool nn_astparser_ruleanonfunc(NNAstParser* prs, bool canassign);
static bool nn_astparser_ruleand(NNAstParser* prs, NNAstToken previous, bool canassign);
static NNAstRule* nn_astparser_putrule(NNAstRule* dest, NNAstParsePrefixFN prefix, NNAstParseInfixFN infix, NNAstPrecedence precedence);
static bool nn_astparser_ruleanonclass(NNAstParser* prs, bool canassign);
static void nn_astparser_parsefuncparamlist(NNAstParser* prs, NNAstFuncCompiler* fnc);


void nn_blob_init(NNState* state, NNBlob* blob)
{
    blob->pstate = state;
    blob->count = 0;
    blob->capacity = 0;
    blob->instrucs = NULL;
    nn_valarray_init(state, &blob->constants);
    nn_valarray_init(state, &blob->argdefvals);
}

void nn_blob_push(NNBlob* blob, NNInstruction ins)
{
    int oldcapacity;
    if(blob->capacity < blob->count + 1)
    {
        oldcapacity = blob->capacity;
        blob->capacity = NN_ASTPARSER_GROWCAPACITY(oldcapacity);
        blob->instrucs = (NNInstruction*)nn_memory_realloc(blob->instrucs, blob->capacity * sizeof(NNInstruction));
    }
    blob->instrucs[blob->count] = ins;
    blob->count++;
}

void nn_blob_destroy(NNBlob* blob)
{
    if(blob->instrucs != NULL)
    {
        nn_memory_free(blob->instrucs);
    }
    nn_valarray_destroy(&blob->constants, false);
    nn_valarray_destroy(&blob->argdefvals, false);
}

int nn_blob_pushconst(NNBlob* blob, NNValue value)
{
    nn_valarray_push(&blob->constants, value);
    return nn_valarray_count(&blob->constants) - 1;
}


/*
* allows for the lexer to created on the stack.
*/
void nn_astlex_init(NNAstLexer* lex, NNState* state, const char* source)
{
    lex->pstate = state;
    lex->sourceptr = source;
    lex->start = source;
    lex->sourceptr = lex->start;
    lex->line = 1;
    lex->tplstringcount = -1;
    lex->onstack = true;
}

NNAstLexer* nn_astlex_make(NNState* state, const char* source)
{
    NNAstLexer* lex;
    lex = (NNAstLexer*)nn_memory_malloc(sizeof(NNAstLexer));
    nn_astlex_init(lex, state, source);
    lex->onstack = false;
    return lex;
}

void nn_astlex_destroy(NNState* state, NNAstLexer* lex)
{
    (void)state;
    if(!lex->onstack)
    {
        nn_memory_free(lex);
    }
}

bool nn_astlex_isatend(NNAstLexer* lex)
{
    return *lex->sourceptr == '\0';
}

static NNAstToken nn_astlex_createtoken(NNAstLexer* lex, NNAstTokType type)
{
    NNAstToken t;
    t.isglobal = false;
    t.type = type;
    t.start = lex->start;
    t.length = (int)(lex->sourceptr - lex->start);
    t.line = lex->line;
    return t;
}

static NNAstToken nn_astlex_errortoken(NNAstLexer* lex, const char* fmt, ...)
{
    int length;
    char* buf;
    va_list va;
    NNAstToken t;
    va_start(va, fmt);
    buf = (char*)nn_memory_malloc(sizeof(char) * 1024);
    /* TODO: used to be vasprintf. need to check how much to actually allocate! */
    length = vsprintf(buf, fmt, va);
    va_end(va);
    t.type = NEON_ASTTOK_ERROR;
    t.start = buf;
    t.isglobal = false;
    if(buf != NULL)
    {
        t.length = length;
    }
    else
    {
        t.length = 0;
    }
    t.line = lex->line;
    return t;
}

static bool nn_astutil_isdigit(char c)
{
    return c >= '0' && c <= '9';
}

static bool nn_astutil_isbinary(char c)
{
    return c == '0' || c == '1';
}

static bool nn_astutil_isalpha(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static bool nn_astutil_isoctal(char c)
{
    return c >= '0' && c <= '7';
}

static bool nn_astutil_ishexadecimal(char c)
{
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

const char* nn_astutil_toktype2str(int t)
{
    switch(t)
    {
        case NEON_ASTTOK_NEWLINE: return "NEON_ASTTOK_NEWLINE";
        case NEON_ASTTOK_PARENOPEN: return "NEON_ASTTOK_PARENOPEN";
        case NEON_ASTTOK_PARENCLOSE: return "NEON_ASTTOK_PARENCLOSE";
        case NEON_ASTTOK_BRACKETOPEN: return "NEON_ASTTOK_BRACKETOPEN";
        case NEON_ASTTOK_BRACKETCLOSE: return "NEON_ASTTOK_BRACKETCLOSE";
        case NEON_ASTTOK_BRACEOPEN: return "NEON_ASTTOK_BRACEOPEN";
        case NEON_ASTTOK_BRACECLOSE: return "NEON_ASTTOK_BRACECLOSE";
        case NEON_ASTTOK_SEMICOLON: return "NEON_ASTTOK_SEMICOLON";
        case NEON_ASTTOK_COMMA: return "NEON_ASTTOK_COMMA";
        case NEON_ASTTOK_BACKSLASH: return "NEON_ASTTOK_BACKSLASH";
        case NEON_ASTTOK_EXCLMARK: return "NEON_ASTTOK_EXCLMARK";
        case NEON_ASTTOK_NOTEQUAL: return "NEON_ASTTOK_NOTEQUAL";
        case NEON_ASTTOK_COLON: return "NEON_ASTTOK_COLON";
        case NEON_ASTTOK_AT: return "NEON_ASTTOK_AT";
        case NEON_ASTTOK_DOT: return "NEON_ASTTOK_DOT";
        case NEON_ASTTOK_DOUBLEDOT: return "NEON_ASTTOK_DOUBLEDOT";
        case NEON_ASTTOK_TRIPLEDOT: return "NEON_ASTTOK_TRIPLEDOT";
        case NEON_ASTTOK_PLUS: return "NEON_ASTTOK_PLUS";
        case NEON_ASTTOK_PLUSASSIGN: return "NEON_ASTTOK_PLUSASSIGN";
        case NEON_ASTTOK_INCREMENT: return "NEON_ASTTOK_INCREMENT";
        case NEON_ASTTOK_MINUS: return "NEON_ASTTOK_MINUS";
        case NEON_ASTTOK_MINUSASSIGN: return "NEON_ASTTOK_MINUSASSIGN";
        case NEON_ASTTOK_DECREMENT: return "NEON_ASTTOK_DECREMENT";
        case NEON_ASTTOK_MULTIPLY: return "NEON_ASTTOK_MULTIPLY";
        case NEON_ASTTOK_MULTASSIGN: return "NEON_ASTTOK_MULTASSIGN";
        case NEON_ASTTOK_POWEROF: return "NEON_ASTTOK_POWEROF";
        case NEON_ASTTOK_POWASSIGN: return "NEON_ASTTOK_POWASSIGN";
        case NEON_ASTTOK_DIVIDE: return "NEON_ASTTOK_DIVIDE";
        case NEON_ASTTOK_DIVASSIGN: return "NEON_ASTTOK_DIVASSIGN";
        case NEON_ASTTOK_FLOOR: return "NEON_ASTTOK_FLOOR";
        case NEON_ASTTOK_ASSIGN: return "NEON_ASTTOK_ASSIGN";
        case NEON_ASTTOK_EQUAL: return "NEON_ASTTOK_EQUAL";
        case NEON_ASTTOK_LESSTHAN: return "NEON_ASTTOK_LESSTHAN";
        case NEON_ASTTOK_LESSEQUAL: return "NEON_ASTTOK_LESSEQUAL";
        case NEON_ASTTOK_LEFTSHIFT: return "NEON_ASTTOK_LEFTSHIFT";
        case NEON_ASTTOK_LEFTSHIFTASSIGN: return "NEON_ASTTOK_LEFTSHIFTASSIGN";
        case NEON_ASTTOK_GREATERTHAN: return "NEON_ASTTOK_GREATERTHAN";
        case NEON_ASTTOK_GREATER_EQ: return "NEON_ASTTOK_GREATER_EQ";
        case NEON_ASTTOK_RIGHTSHIFT: return "NEON_ASTTOK_RIGHTSHIFT";
        case NEON_ASTTOK_RIGHTSHIFTASSIGN: return "NEON_ASTTOK_RIGHTSHIFTASSIGN";
        case NEON_ASTTOK_MODULO: return "NEON_ASTTOK_MODULO";
        case NEON_ASTTOK_PERCENT_EQ: return "NEON_ASTTOK_PERCENT_EQ";
        case NEON_ASTTOK_AMP: return "NEON_ASTTOK_AMP";
        case NEON_ASTTOK_AMP_EQ: return "NEON_ASTTOK_AMP_EQ";
        case NEON_ASTTOK_BAR: return "NEON_ASTTOK_BAR";
        case NEON_ASTTOK_BAR_EQ: return "NEON_ASTTOK_BAR_EQ";
        case NEON_ASTTOK_TILDE: return "NEON_ASTTOK_TILDE";
        case NEON_ASTTOK_TILDE_EQ: return "NEON_ASTTOK_TILDE_EQ";
        case NEON_ASTTOK_XOR: return "NEON_ASTTOK_XOR";
        case NEON_ASTTOK_XOR_EQ: return "NEON_ASTTOK_XOR_EQ";
        case NEON_ASTTOK_QUESTION: return "NEON_ASTTOK_QUESTION";
        case NEON_ASTTOK_KWAND: return "NEON_ASTTOK_KWAND";
        case NEON_ASTTOK_KWAS: return "NEON_ASTTOK_KWAS";
        case NEON_ASTTOK_KWASSERT: return "NEON_ASTTOK_KWASSERT";
        case NEON_ASTTOK_KWBREAK: return "NEON_ASTTOK_KWBREAK";
        case NEON_ASTTOK_KWCATCH: return "NEON_ASTTOK_KWCATCH";
        case NEON_ASTTOK_KWCLASS: return "NEON_ASTTOK_KWCLASS";
        case NEON_ASTTOK_KWCONTINUE: return "NEON_ASTTOK_KWCONTINUE";
        case NEON_ASTTOK_KWFUNCTION: return "NEON_ASTTOK_KWFUNCTION";
        case NEON_ASTTOK_KWDEFAULT: return "NEON_ASTTOK_KWDEFAULT";
        case NEON_ASTTOK_KWTHROW: return "NEON_ASTTOK_KWTHROW";
        case NEON_ASTTOK_KWDO: return "NEON_ASTTOK_KWDO";
        case NEON_ASTTOK_KWECHO: return "NEON_ASTTOK_KWECHO";
        case NEON_ASTTOK_KWELSE: return "NEON_ASTTOK_KWELSE";
        case NEON_ASTTOK_KWFALSE: return "NEON_ASTTOK_KWFALSE";
        case NEON_ASTTOK_KWFINALLY: return "NEON_ASTTOK_KWFINALLY";
        case NEON_ASTTOK_KWFOREACH: return "NEON_ASTTOK_KWFOREACH";
        case NEON_ASTTOK_KWIF: return "NEON_ASTTOK_KWIF";
        case NEON_ASTTOK_KWIMPORT: return "NEON_ASTTOK_KWIMPORT";
        case NEON_ASTTOK_KWIN: return "NEON_ASTTOK_KWIN";
        case NEON_ASTTOK_KWFOR: return "NEON_ASTTOK_KWFOR";
        case NEON_ASTTOK_KWNULL: return "NEON_ASTTOK_KWNULL";
        case NEON_ASTTOK_KWNEW: return "NEON_ASTTOK_KWNEW";
        case NEON_ASTTOK_KWOR: return "NEON_ASTTOK_KWOR";
        case NEON_ASTTOK_KWSUPER: return "NEON_ASTTOK_KWSUPER";
        case NEON_ASTTOK_KWRETURN: return "NEON_ASTTOK_KWRETURN";
        case NEON_ASTTOK_KWTHIS: return "NEON_ASTTOK_KWTHIS";
        case NEON_ASTTOK_KWSTATIC: return "NEON_ASTTOK_KWSTATIC";
        case NEON_ASTTOK_KWTRUE: return "NEON_ASTTOK_KWTRUE";
        case NEON_ASTTOK_KWTRY: return "NEON_ASTTOK_KWTRY";
        case NEON_ASTTOK_KWSWITCH: return "NEON_ASTTOK_KWSWITCH";
        case NEON_ASTTOK_KWVAR: return "NEON_ASTTOK_KWVAR";
        case NEON_ASTTOK_KWCONST: return "NEON_ASTTOK_KWCONST";
        case NEON_ASTTOK_KWCASE: return "NEON_ASTTOK_KWCASE";
        case NEON_ASTTOK_KWWHILE: return "NEON_ASTTOK_KWWHILE";
        case NEON_ASTTOK_KWINSTANCEOF: return "NEON_ASTTOK_KWINSTANCEOF";
        case NEON_ASTTOK_KWEXTENDS: return "NEON_ASTTOK_KWEXTENDS";
        case NEON_ASTTOK_LITERALSTRING: return "NEON_ASTTOK_LITERALSTRING";
        case NEON_ASTTOK_LITERALRAWSTRING: return "NEON_ASTTOK_LITERALRAWSTRING";
        case NEON_ASTTOK_LITNUMREG: return "NEON_ASTTOK_LITNUMREG";
        case NEON_ASTTOK_LITNUMBIN: return "NEON_ASTTOK_LITNUMBIN";
        case NEON_ASTTOK_LITNUMOCT: return "NEON_ASTTOK_LITNUMOCT";
        case NEON_ASTTOK_LITNUMHEX: return "NEON_ASTTOK_LITNUMHEX";
        case NEON_ASTTOK_IDENTNORMAL: return "NEON_ASTTOK_IDENTNORMAL";
        case NEON_ASTTOK_DECORATOR: return "NEON_ASTTOK_DECORATOR";
        case NEON_ASTTOK_INTERPOLATION: return "NEON_ASTTOK_INTERPOLATION";
        case NEON_ASTTOK_EOF: return "NEON_ASTTOK_EOF";
        case NEON_ASTTOK_ERROR: return "NEON_ASTTOK_ERROR";
        case NEON_ASTTOK_KWEMPTY: return "NEON_ASTTOK_KWEMPTY";
        case NEON_ASTTOK_UNDEFINED: return "NEON_ASTTOK_UNDEFINED";
        case NEON_ASTTOK_TOKCOUNT: return "NEON_ASTTOK_TOKCOUNT";
    }
    return "?invalid?";
}

static char nn_astlex_advance(NNAstLexer* lex)
{
    lex->sourceptr++;
    if(lex->sourceptr[-1] == '\n')
    {
        lex->line++;
    }
    return lex->sourceptr[-1];
}

static bool nn_astlex_match(NNAstLexer* lex, char expected)
{
    if(nn_astlex_isatend(lex))
    {
        return false;
    }
    if(*lex->sourceptr != expected)
    {
        return false;
    }
    lex->sourceptr++;
    if(lex->sourceptr[-1] == '\n')
    {
        lex->line++;
    }
    return true;
}

static char nn_astlex_peekcurr(NNAstLexer* lex)
{
    return *lex->sourceptr;
}

static char nn_astlex_peekprev(NNAstLexer* lex)
{
    if(lex->sourceptr == lex->start)
    {
        return -1;
    }
    return lex->sourceptr[-1];
}

static char nn_astlex_peeknext(NNAstLexer* lex)
{
    if(nn_astlex_isatend(lex))
    {
        return '\0';
    }
    return lex->sourceptr[1];
}

static NNAstToken nn_astlex_skipblockcomments(NNAstLexer* lex)
{
    int nesting;
    nesting = 1;
    while(nesting > 0)
    {
        if(nn_astlex_isatend(lex))
        {
            return nn_astlex_errortoken(lex, "unclosed block comment");
        }
        /* internal comment open */
        if(nn_astlex_peekcurr(lex) == '/' && nn_astlex_peeknext(lex) == '*')
        {
            nn_astlex_advance(lex);
            nn_astlex_advance(lex);
            nesting++;
            continue;
        }
        /* comment close */
        if(nn_astlex_peekcurr(lex) == '*' && nn_astlex_peeknext(lex) == '/')
        {
            nn_astlex_advance(lex);
            nn_astlex_advance(lex);
            nesting--;
            continue;
        }
        /* regular comment body */
        nn_astlex_advance(lex);
    }
    #if defined(NEON_PLAT_ISWINDOWS)
        #if 0
            nn_astlex_advance(lex);
        #endif
    #endif
    return nn_astlex_createtoken(lex, NEON_ASTTOK_UNDEFINED);
}

static NNAstToken nn_astlex_skipspace(NNAstLexer* lex)
{
    char c;
    NNAstToken result;
    result.isglobal = false;
    for(;;)
    {
        c = nn_astlex_peekcurr(lex);
        switch(c)
        {
            case ' ':
            case '\r':
            case '\t':
            {
                nn_astlex_advance(lex);
            }
            break;
            /*
            case '\n':
                {
                    lex->line++;
                    nn_astlex_advance(lex);
                }
                break;
            */
            /*
            case '#':
                // single line comment
                {
                    while(nn_astlex_peekcurr(lex) != '\n' && !nn_astlex_isatend(lex))
                        nn_astlex_advance(lex);

                }
                break;
            */
            case '/':
            {
                if(nn_astlex_peeknext(lex) == '/')
                {
                    while(nn_astlex_peekcurr(lex) != '\n' && !nn_astlex_isatend(lex))
                    {
                        nn_astlex_advance(lex);
                    }
                    return nn_astlex_createtoken(lex, NEON_ASTTOK_UNDEFINED);
                }
                else if(nn_astlex_peeknext(lex) == '*')
                {
                    nn_astlex_advance(lex);
                    nn_astlex_advance(lex);
                    result = nn_astlex_skipblockcomments(lex);
                    if(result.type != NEON_ASTTOK_UNDEFINED)
                    {
                        return result;
                    }
                    break;
                }
                else
                {
                    return nn_astlex_createtoken(lex, NEON_ASTTOK_UNDEFINED);
                }
            }
            break;
            /* exit as soon as we see a non-whitespace... */
            default:
                goto finished;
                break;
        }
    }
    finished:
    return nn_astlex_createtoken(lex, NEON_ASTTOK_UNDEFINED);
}

NNAstToken nn_astlex_scanstring(NNAstLexer* lex, char quote, bool withtemplate, bool permitescapes)
{
    NNAstToken tkn;
    while(nn_astlex_peekcurr(lex) != quote && !nn_astlex_isatend(lex))
    {
        if(withtemplate)
        {
            /* interpolation started */
            if(nn_astlex_peekcurr(lex) == '$' && nn_astlex_peeknext(lex) == '{' && nn_astlex_peekprev(lex) != '\\')
            {
                if(lex->tplstringcount - 1 < NEON_CONFIG_ASTMAXSTRTPLDEPTH)
                {
                    lex->tplstringcount++;
                    lex->tplstringbuffer[lex->tplstringcount] = (int)quote;
                    lex->sourceptr++;
                    tkn = nn_astlex_createtoken(lex, NEON_ASTTOK_INTERPOLATION);
                    lex->sourceptr++;
                    return tkn;
                }
                return nn_astlex_errortoken(lex, "maximum interpolation nesting of %d exceeded by %d", NEON_CONFIG_ASTMAXSTRTPLDEPTH,
                    NEON_CONFIG_ASTMAXSTRTPLDEPTH - lex->tplstringcount + 1);
            }
        }
        if(nn_astlex_peekcurr(lex) == '\\' && (nn_astlex_peeknext(lex) == quote || nn_astlex_peeknext(lex) == '\\'))
        {
            nn_astlex_advance(lex);
        }
        nn_astlex_advance(lex);
    }
    if(nn_astlex_isatend(lex))
    {
        return nn_astlex_errortoken(lex, "unterminated string (opening quote not matched)");
    }
    /* the closing quote */
    nn_astlex_match(lex, quote);
    if(permitescapes)
    {
        return nn_astlex_createtoken(lex, NEON_ASTTOK_LITERALSTRING);
    }
    return nn_astlex_createtoken(lex, NEON_ASTTOK_LITERALRAWSTRING);
}

NNAstToken nn_astlex_scannumber(NNAstLexer* lex)
{
    /* handle binary, octal and hexadecimals */
    if(nn_astlex_peekprev(lex) == '0')
    {
        /* binary number */
        if(nn_astlex_match(lex, 'b'))
        {
            while(nn_astutil_isbinary(nn_astlex_peekcurr(lex)))
            {
                nn_astlex_advance(lex);
            }
            return nn_astlex_createtoken(lex, NEON_ASTTOK_LITNUMBIN);
        }
        else if(nn_astlex_match(lex, 'c'))
        {
            while(nn_astutil_isoctal(nn_astlex_peekcurr(lex)))
            {
                nn_astlex_advance(lex);
            }
            return nn_astlex_createtoken(lex, NEON_ASTTOK_LITNUMOCT);
        }
        else if(nn_astlex_match(lex, 'x'))
        {
            while(nn_astutil_ishexadecimal(nn_astlex_peekcurr(lex)))
            {
                nn_astlex_advance(lex);
            }
            return nn_astlex_createtoken(lex, NEON_ASTTOK_LITNUMHEX);
        }
    }
    while(nn_astutil_isdigit(nn_astlex_peekcurr(lex)))
    {
        nn_astlex_advance(lex);
    }
    /* dots(.) are only valid here when followed by a digit */
    if(nn_astlex_peekcurr(lex) == '.' && nn_astutil_isdigit(nn_astlex_peeknext(lex)))
    {
        nn_astlex_advance(lex);
        while(nn_astutil_isdigit(nn_astlex_peekcurr(lex)))
        {
            nn_astlex_advance(lex);
        }
        /*
        // E or e are only valid here when followed by a digit and occurring after a dot
        */
        if((nn_astlex_peekcurr(lex) == 'e' || nn_astlex_peekcurr(lex) == 'E') && (nn_astlex_peeknext(lex) == '+' || nn_astlex_peeknext(lex) == '-'))
        {
            nn_astlex_advance(lex);
            nn_astlex_advance(lex);
            while(nn_astutil_isdigit(nn_astlex_peekcurr(lex)))
            {
                nn_astlex_advance(lex);
            }
        }
    }
    return nn_astlex_createtoken(lex, NEON_ASTTOK_LITNUMREG);
}

static NNAstTokType nn_astlex_getidenttype(NNAstLexer* lex)
{
    static const struct
    {
        const char* str;
        int tokid;
    }
    keywords[] =
    {
        { "and", NEON_ASTTOK_KWAND },
        { "assert", NEON_ASTTOK_KWASSERT },
        { "as", NEON_ASTTOK_KWAS },
        { "break", NEON_ASTTOK_KWBREAK },
        { "catch", NEON_ASTTOK_KWCATCH },
        { "class", NEON_ASTTOK_KWCLASS },
        { "continue", NEON_ASTTOK_KWCONTINUE },
        { "default", NEON_ASTTOK_KWDEFAULT },
        { "def", NEON_ASTTOK_KWFUNCTION },
        { "function", NEON_ASTTOK_KWFUNCTION },
        { "throw", NEON_ASTTOK_KWTHROW },
        { "do", NEON_ASTTOK_KWDO },
        { "echo", NEON_ASTTOK_KWECHO },
        { "else", NEON_ASTTOK_KWELSE },
        { "empty", NEON_ASTTOK_KWEMPTY },
        { "extends", NEON_ASTTOK_KWEXTENDS },
        { "false", NEON_ASTTOK_KWFALSE },
        { "finally", NEON_ASTTOK_KWFINALLY },
        { "foreach", NEON_ASTTOK_KWFOREACH },
        { "if", NEON_ASTTOK_KWIF },
        { "import", NEON_ASTTOK_KWIMPORT },
        { "in", NEON_ASTTOK_KWIN },
        { "instanceof", NEON_ASTTOK_KWINSTANCEOF},
        { "for", NEON_ASTTOK_KWFOR },
        { "null", NEON_ASTTOK_KWNULL },
        { "new", NEON_ASTTOK_KWNEW },
        { "or", NEON_ASTTOK_KWOR },
        { "super", NEON_ASTTOK_KWSUPER },
        { "return", NEON_ASTTOK_KWRETURN },
        { "this", NEON_ASTTOK_KWTHIS },
        { "static", NEON_ASTTOK_KWSTATIC },
        { "true", NEON_ASTTOK_KWTRUE },
        { "try", NEON_ASTTOK_KWTRY },
        { "typeof", NEON_ASTTOK_KWTYPEOF },
        { "switch", NEON_ASTTOK_KWSWITCH },
        { "case", NEON_ASTTOK_KWCASE },
        { "var", NEON_ASTTOK_KWVAR },
        { "let", NEON_ASTTOK_KWVAR },
        { "const", NEON_ASTTOK_KWCONST },
        { "while", NEON_ASTTOK_KWWHILE },
        { NULL, (NNAstTokType)0 }
    };
    size_t i;
    size_t kwlen;
    size_t ofs;
    const char* kwtext;
    for(i = 0; keywords[i].str != NULL; i++)
    {
        kwtext = keywords[i].str;
        kwlen = strlen(kwtext);
        ofs = (lex->sourceptr - lex->start);
        if(ofs == kwlen)
        {
            if(memcmp(lex->start, kwtext, kwlen) == 0)
            {
                return (NNAstTokType)keywords[i].tokid;
            }
        }
    }
    return NEON_ASTTOK_IDENTNORMAL;
}

NNAstToken nn_astlex_scanident(NNAstLexer* lex, bool isdollar)
{
    int cur;
    NNAstToken tok;
    cur = nn_astlex_peekcurr(lex);
    if(cur == '$')
    {
        nn_astlex_advance(lex);
    }
    while(true)
    {
        cur = nn_astlex_peekcurr(lex);
        if(nn_astutil_isalpha(cur) || nn_astutil_isdigit(cur))
        {
            nn_astlex_advance(lex);
        }
        else
        {
            break;
        }
    }
    tok = nn_astlex_createtoken(lex, nn_astlex_getidenttype(lex));
    tok.isglobal = isdollar;
    return tok;
}

static NNAstToken nn_astlex_scandecorator(NNAstLexer* lex)
{
    while(nn_astutil_isalpha(nn_astlex_peekcurr(lex)) || nn_astutil_isdigit(nn_astlex_peekcurr(lex)))
    {
        nn_astlex_advance(lex);
    }
    return nn_astlex_createtoken(lex, NEON_ASTTOK_DECORATOR);
}

NNAstToken nn_astlex_scantoken(NNAstLexer* lex)
{
    char c;
    bool isdollar;
    NNAstToken tk;
    NNAstToken token;
    tk = nn_astlex_skipspace(lex);
    if(tk.type != NEON_ASTTOK_UNDEFINED)
    {
        return tk;
    }
    lex->start = lex->sourceptr;
    if(nn_astlex_isatend(lex))
    {
        return nn_astlex_createtoken(lex, NEON_ASTTOK_EOF);
    }
    c = nn_astlex_advance(lex);
    if(nn_astutil_isdigit(c))
    {
        return nn_astlex_scannumber(lex);
    }
    else if(nn_astutil_isalpha(c) || (c == '$'))
    {
        isdollar = (c == '$');
        return nn_astlex_scanident(lex, isdollar);
    }
    switch(c)
    {
        case '(':
            {
                return nn_astlex_createtoken(lex, NEON_ASTTOK_PARENOPEN);
            }
            break;
        case ')':
            {
                return nn_astlex_createtoken(lex, NEON_ASTTOK_PARENCLOSE);
            }
            break;
        case '[':
            {
                return nn_astlex_createtoken(lex, NEON_ASTTOK_BRACKETOPEN);
            }
            break;
        case ']':
            {
                return nn_astlex_createtoken(lex, NEON_ASTTOK_BRACKETCLOSE);
            }
            break;
        case '{':
            {
                return nn_astlex_createtoken(lex, NEON_ASTTOK_BRACEOPEN);
            }
            break;
        case '}':
            {
                if(lex->tplstringcount > -1)
                {
                    token = nn_astlex_scanstring(lex, (char)lex->tplstringbuffer[lex->tplstringcount], true, true);
                    lex->tplstringcount--;
                    return token;
                }
                return nn_astlex_createtoken(lex, NEON_ASTTOK_BRACECLOSE);
            }
            break;
        case ';':
            {
                return nn_astlex_createtoken(lex, NEON_ASTTOK_SEMICOLON);
            }
            break;
        case '\\':
            {
                return nn_astlex_createtoken(lex, NEON_ASTTOK_BACKSLASH);
            }
            break;
        case ':':
            {
                return nn_astlex_createtoken(lex, NEON_ASTTOK_COLON);
            }
            break;
        case ',':
            {
                return nn_astlex_createtoken(lex, NEON_ASTTOK_COMMA);
            }
            break;
        case '@':
            {
                if(!nn_astutil_isalpha(nn_astlex_peekcurr(lex)))
                {
                    return nn_astlex_createtoken(lex, NEON_ASTTOK_AT);
                }
                return nn_astlex_scandecorator(lex);
            }
            break;
        case '!':
            {
                if(nn_astlex_match(lex, '='))
                {
                    /* pseudo-handle '!==' */
                    if(nn_astlex_match(lex, '='))
                    {
                        return nn_astlex_createtoken(lex, NEON_ASTTOK_NOTEQUAL);
                    }
                    return nn_astlex_createtoken(lex, NEON_ASTTOK_NOTEQUAL);
                }
                return nn_astlex_createtoken(lex, NEON_ASTTOK_EXCLMARK);

            }
            break;
        case '.':
            {
                if(nn_astlex_match(lex, '.'))
                {
                    if(nn_astlex_match(lex, '.'))
                    {
                        return nn_astlex_createtoken(lex, NEON_ASTTOK_TRIPLEDOT);
                    }
                    return nn_astlex_createtoken(lex, NEON_ASTTOK_DOUBLEDOT);
                }
                return nn_astlex_createtoken(lex, NEON_ASTTOK_DOT);
            }
            break;
        case '+':
        {
            if(nn_astlex_match(lex, '+'))
            {
                return nn_astlex_createtoken(lex, NEON_ASTTOK_INCREMENT);
            }
            if(nn_astlex_match(lex, '='))
            {
                return nn_astlex_createtoken(lex, NEON_ASTTOK_PLUSASSIGN);
            }
            else
            {
                return nn_astlex_createtoken(lex, NEON_ASTTOK_PLUS);
            }
        }
        break;
        case '-':
            {
                if(nn_astlex_match(lex, '-'))
                {
                    return nn_astlex_createtoken(lex, NEON_ASTTOK_DECREMENT);
                }
                if(nn_astlex_match(lex, '='))
                {
                    return nn_astlex_createtoken(lex, NEON_ASTTOK_MINUSASSIGN);
                }
                else
                {
                    return nn_astlex_createtoken(lex, NEON_ASTTOK_MINUS);
                }
            }
            break;
        case '*':
            {
                if(nn_astlex_match(lex, '*'))
                {
                    if(nn_astlex_match(lex, '='))
                    {
                        return nn_astlex_createtoken(lex, NEON_ASTTOK_POWASSIGN);
                    }
                    return nn_astlex_createtoken(lex, NEON_ASTTOK_POWEROF);
                }
                else
                {
                    if(nn_astlex_match(lex, '='))
                    {
                        return nn_astlex_createtoken(lex, NEON_ASTTOK_MULTASSIGN);
                    }
                    return nn_astlex_createtoken(lex, NEON_ASTTOK_MULTIPLY);
                }
            }
            break;
        case '/':
            {
                if(nn_astlex_match(lex, '='))
                {
                    return nn_astlex_createtoken(lex, NEON_ASTTOK_DIVASSIGN);
                }
                return nn_astlex_createtoken(lex, NEON_ASTTOK_DIVIDE);
            }
            break;
        case '=':
            {
                if(nn_astlex_match(lex, '='))
                {
                    /* pseudo-handle === */
                    if(nn_astlex_match(lex, '='))
                    {
                        return nn_astlex_createtoken(lex, NEON_ASTTOK_EQUAL);
                    }
                    return nn_astlex_createtoken(lex, NEON_ASTTOK_EQUAL);
                }
                return nn_astlex_createtoken(lex, NEON_ASTTOK_ASSIGN);
            }        
            break;
        case '<':
            {
                if(nn_astlex_match(lex, '<'))
                {
                    if(nn_astlex_match(lex, '='))
                    {
                        return nn_astlex_createtoken(lex, NEON_ASTTOK_LEFTSHIFTASSIGN);
                    }
                    return nn_astlex_createtoken(lex, NEON_ASTTOK_LEFTSHIFT);
                }
                else
                {
                    if(nn_astlex_match(lex, '='))
                    {
                        return nn_astlex_createtoken(lex, NEON_ASTTOK_LESSEQUAL);
                    }
                    return nn_astlex_createtoken(lex, NEON_ASTTOK_LESSTHAN);

                }
            }
            break;
        case '>':
            {
                if(nn_astlex_match(lex, '>'))
                {
                    if(nn_astlex_match(lex, '='))
                    {
                        return nn_astlex_createtoken(lex, NEON_ASTTOK_RIGHTSHIFTASSIGN);
                    }
                    return nn_astlex_createtoken(lex, NEON_ASTTOK_RIGHTSHIFT);
                }
                else
                {
                    if(nn_astlex_match(lex, '='))
                    {
                        return nn_astlex_createtoken(lex, NEON_ASTTOK_GREATER_EQ);
                    }
                    return nn_astlex_createtoken(lex, NEON_ASTTOK_GREATERTHAN);
                }
            }
            break;
        case '%':
            {
                if(nn_astlex_match(lex, '='))
                {
                    return nn_astlex_createtoken(lex, NEON_ASTTOK_PERCENT_EQ);
                }
                return nn_astlex_createtoken(lex, NEON_ASTTOK_MODULO);
            }
            break;
        case '&':
            {
                if(nn_astlex_match(lex, '&'))
                {
                    return nn_astlex_createtoken(lex, NEON_ASTTOK_KWAND);
                }
                else if(nn_astlex_match(lex, '='))
                {
                    return nn_astlex_createtoken(lex, NEON_ASTTOK_AMP_EQ);
                }
                return nn_astlex_createtoken(lex, NEON_ASTTOK_AMP);
            }
            break;
        case '|':
            {
                if(nn_astlex_match(lex, '|'))
                {
                    return nn_astlex_createtoken(lex, NEON_ASTTOK_KWOR);
                }
                else if(nn_astlex_match(lex, '='))
                {
                    return nn_astlex_createtoken(lex, NEON_ASTTOK_BAR_EQ);
                }
                return nn_astlex_createtoken(lex, NEON_ASTTOK_BAR);
            }
            break;
        case '~':
            {
                if(nn_astlex_match(lex, '='))
                {
                    return nn_astlex_createtoken(lex, NEON_ASTTOK_TILDE_EQ);
                }
                return nn_astlex_createtoken(lex, NEON_ASTTOK_TILDE);
            }
            break;
        case '^':
            {
                if(nn_astlex_match(lex, '='))
                {
                    return nn_astlex_createtoken(lex, NEON_ASTTOK_XOR_EQ);
                }
                return nn_astlex_createtoken(lex, NEON_ASTTOK_XOR);
            }
            break;
        case '\n':
            {
                return nn_astlex_createtoken(lex, NEON_ASTTOK_NEWLINE);
            }
            break;
        case '"':
            {
                return nn_astlex_scanstring(lex, '"', false, true);
            }
            break;
        case '\'':
            {
                return nn_astlex_scanstring(lex, '\'', false, false);
            }
            break;
        case '`':
            {
                return nn_astlex_scanstring(lex, '`', true, true);
            }
            break;
        case '?':
            {
                return nn_astlex_createtoken(lex, NEON_ASTTOK_QUESTION);
            }
            break;
        /*
        // --- DO NOT MOVE ABOVE OR BELOW THE DEFAULT CASE ---
        // fall-through tokens goes here... this tokens are only valid
        // when the carry another token with them...
        // be careful not to add break after them so that they may use the default
        // case.
        */
        default:
            break;
    }
    return nn_astlex_errortoken(lex, "unexpected character %c", c);
}

NNAstParser* nn_astparser_makeparser(NNState* state, NNAstLexer* lexer, NNObjModule* module, bool keeplast)
{
    NNAstParser* parser;
    parser = (NNAstParser*)nn_memory_malloc(sizeof(NNAstParser));
    parser->pstate = state;
    parser->lexer = lexer;
    parser->currentfunccompiler = NULL;
    parser->haderror = false;
    parser->panicmode = false;
    parser->stopprintingsyntaxerrors = false;
    parser->blockcount = 0;
    parser->errorcount = 0;
    parser->replcanecho = false;
    parser->isreturning = false;
    parser->istrying = false;
    parser->compcontext = NEON_COMPCONTEXT_NONE;
    parser->innermostloopstart = -1;
    parser->innermostloopscopedepth = 0;
    parser->currentclasscompiler = NULL;
    parser->currentmodule = module;
    parser->keeplastvalue = keeplast;
    parser->lastwasstatement = false;
    parser->infunction = false;
    parser->inswitch = false;
    parser->currentfile = nn_string_getdata(parser->currentmodule->physicalpath);
    return parser;
}

void nn_astparser_destroy(NNAstParser* parser)
{
    nn_memory_free(parser);
}

static NNBlob* nn_astparser_currentblob(NNAstParser* prs)
{
    return &prs->currentfunccompiler->targetfunc->fnscriptfunc.blob;
}

static bool nn_astparser_raiseerroratv(NNAstParser* prs, NNAstToken* t, const char* message, va_list args)
{
    const char* colred;
    const char* colreset;    
    colred = nn_util_color(NEON_COLOR_RED);
    colreset = nn_util_color(NEON_COLOR_RESET);
    fflush(stdout);
    if(prs->stopprintingsyntaxerrors)
    {
        return false;
    }
    if((prs->pstate->conf.maxsyntaxerrors != 0) && (prs->errorcount >= prs->pstate->conf.maxsyntaxerrors))
    {
        fprintf(stderr, "%stoo many errors emitted%s (maximum is %d)\n", colred, colreset, prs->pstate->conf.maxsyntaxerrors);
        prs->stopprintingsyntaxerrors = true;
        return false;
    }
    /*
    // do not cascade error
    // suppress error if already in panic mode
    */
    if(prs->panicmode)
    {
        return false;
    }
    prs->panicmode = true;
    fprintf(stderr, "(%d) %sSyntaxError%s",  prs->errorcount, colred, colreset);
    fprintf(stderr, " in [%s:%d]: ", nn_string_getdata(prs->currentmodule->physicalpath), t->line);
    vfprintf(stderr, message, args);
    fprintf(stderr, " ");
    if(t->type == NEON_ASTTOK_EOF)
    {
        fprintf(stderr, " at end");
    }
    else if(t->type == NEON_ASTTOK_ERROR)
    {
        /* do nothing */
        fprintf(stderr, "at <internal error>");
    }
    else
    {
        if(t->length == 1 && *t->start == '\n')
        {
            fprintf(stderr, " at newline");
        }
        else
        {
            fprintf(stderr, " at '%.*s'", t->length, t->start);
        }
    }
    fprintf(stderr, "\n");
    prs->haderror = true;
    prs->errorcount++;
    return false;
}

static bool nn_astparser_raiseerror(NNAstParser* prs, const char* message, ...)
{
    va_list args;
    va_start(args, message);
    nn_astparser_raiseerroratv(prs, &prs->prevtoken, message, args);
    va_end(args);
    return false;
}

static bool nn_astparser_raiseerroratcurrent(NNAstParser* prs, const char* message, ...)
{
    va_list args;
    va_start(args, message);
    nn_astparser_raiseerroratv(prs, &prs->currtoken, message, args);
    va_end(args);
    return false;
}

static void nn_astparser_advance(NNAstParser* prs)
{
    prs->prevtoken = prs->currtoken;
    while(true)
    {
        prs->currtoken = nn_astlex_scantoken(prs->lexer);
        if(prs->currtoken.type != NEON_ASTTOK_ERROR)
        {
            break;
        }
        nn_astparser_raiseerroratcurrent(prs, prs->currtoken.start);
    }
}

static bool nn_astparser_consume(NNAstParser* prs, NNAstTokType t, const char* message)
{
    if(nn_astparser_istype(prs->currtoken.type, t))
    {
        nn_astparser_advance(prs);
        return true;
    }
    return nn_astparser_raiseerroratcurrent(prs, message);
}

static void nn_astparser_consumeor(NNAstParser* prs, const char* message, const NNAstTokType* ts, int count)
{
    int i;
    for(i = 0; i < count; i++)
    {
        if(prs->currtoken.type == ts[i])
        {
            nn_astparser_advance(prs);
            return;
        }
    }
    nn_astparser_raiseerroratcurrent(prs, message);
}

static bool nn_astparser_checknumber(NNAstParser* prs)
{
    NNAstTokType t;
    t = prs->prevtoken.type;
    if(t == NEON_ASTTOK_LITNUMREG || t == NEON_ASTTOK_LITNUMOCT || t == NEON_ASTTOK_LITNUMBIN || t == NEON_ASTTOK_LITNUMHEX)
    {
        return true;
    }
    return false;
}


bool nn_astparser_istype(NNAstTokType prev, NNAstTokType t)
{
    if(t == NEON_ASTTOK_IDENTNORMAL)
    {
        if(prev == NEON_ASTTOK_KWCLASS)
        {
            return true;
        }
    }
    return (prev == t);
}

static bool nn_astparser_check(NNAstParser* prs, NNAstTokType t)
{
    return nn_astparser_istype(prs->currtoken.type, t);
}

static bool nn_astparser_match(NNAstParser* prs, NNAstTokType t)
{
    if(!nn_astparser_check(prs, t))
    {
        return false;
    }
    nn_astparser_advance(prs);
    return true;
}

static void nn_astparser_runparser(NNAstParser* parser)
{
    nn_astparser_advance(parser);
    nn_astparser_ignorewhitespace(parser);
    while(!nn_astparser_match(parser, NEON_ASTTOK_EOF))
    {
        nn_astparser_parsedeclaration(parser);
    }
}

static void nn_astparser_parsedeclaration(NNAstParser* prs)
{
    nn_astparser_ignorewhitespace(prs);
    if(nn_astparser_match(prs, NEON_ASTTOK_KWCLASS))
    {
        nn_astparser_parseclassdeclaration(prs, true);
    }
    else if(nn_astparser_match(prs, NEON_ASTTOK_KWFUNCTION))
    {
        nn_astparser_parsefuncdecl(prs);
    }
    else if(nn_astparser_match(prs, NEON_ASTTOK_KWVAR))
    {
        nn_astparser_parsevardecl(prs, false, false);
    }
    else if(nn_astparser_match(prs, NEON_ASTTOK_KWCONST))
    {
        nn_astparser_parsevardecl(prs, false, true);
    }
    else if(nn_astparser_match(prs, NEON_ASTTOK_BRACEOPEN))
    {
        if(!nn_astparser_check(prs, NEON_ASTTOK_NEWLINE) && prs->currentfunccompiler->scopedepth == 0)
        {
            nn_astparser_parseexprstmt(prs, false, true);
        }
        else
        {
            nn_astparser_scopebegin(prs);
            nn_astparser_parseblock(prs);
            nn_astparser_scopeend(prs);
        }
    }
    else
    {
        nn_astparser_parsestmt(prs);
    }
    nn_astparser_ignorewhitespace(prs);
    if(prs->panicmode)
    {
        nn_astparser_synchronize(prs);
    }
    nn_astparser_ignorewhitespace(prs);
}

static void nn_astparser_parsestmt(NNAstParser* prs)
{
    prs->replcanecho = false;
    nn_astparser_ignorewhitespace(prs);
    if(nn_astparser_match(prs, NEON_ASTTOK_KWECHO))
    {
        nn_astparser_parseechostmt(prs);
    }
    else if(nn_astparser_match(prs, NEON_ASTTOK_KWIF))
    {
        nn_astparser_parseifstmt(prs);
    }
    else if(nn_astparser_match(prs, NEON_ASTTOK_KWDO))
    {
        nn_astparser_parsedo_whilestmt(prs);
    }
    else if(nn_astparser_match(prs, NEON_ASTTOK_KWWHILE))
    {
        nn_astparser_parsewhilestmt(prs);
    }
    else if(nn_astparser_match(prs, NEON_ASTTOK_KWFOR))
    {
        nn_astparser_parseforstmt(prs);
    }
    else if(nn_astparser_match(prs, NEON_ASTTOK_KWFOREACH))
    {
        nn_astparser_parseforeachstmt(prs);
    }
    else if(nn_astparser_match(prs, NEON_ASTTOK_KWSWITCH))
    {
        nn_astparser_parseswitchstmt(prs);
    }
    else if(nn_astparser_match(prs, NEON_ASTTOK_KWCONTINUE))
    {
        nn_astparser_parsecontinuestmt(prs);
    }
    else if(nn_astparser_match(prs, NEON_ASTTOK_KWBREAK))
    {
        nn_astparser_parsebreakstmt(prs);
    }
    else if(nn_astparser_match(prs, NEON_ASTTOK_KWRETURN))
    {
        nn_astparser_parsereturnstmt(prs);
    }
    else if(nn_astparser_match(prs, NEON_ASTTOK_KWASSERT))
    {
        nn_astparser_parseassertstmt(prs);
    }
    else if(nn_astparser_match(prs, NEON_ASTTOK_KWTHROW))
    {
        nn_astparser_parsethrowstmt(prs);
    }
    else if(nn_astparser_match(prs, NEON_ASTTOK_BRACEOPEN))
    {
        nn_astparser_scopebegin(prs);
        nn_astparser_parseblock(prs);
        nn_astparser_scopeend(prs);
    }
    else if(nn_astparser_match(prs, NEON_ASTTOK_KWTRY))
    {
        nn_astparser_parsetrystmt(prs);
    }
    else
    {
        nn_astparser_parseexprstmt(prs, false, false);
    }
    nn_astparser_ignorewhitespace(prs);
}

static void nn_astparser_consumestmtend(NNAstParser* prs)
{
    /* allow block last statement to omit statement end */
    if(prs->blockcount > 0 && nn_astparser_check(prs, NEON_ASTTOK_BRACECLOSE))
    {
        return;
    }
    if(nn_astparser_match(prs, NEON_ASTTOK_SEMICOLON))
    {
        while(nn_astparser_match(prs, NEON_ASTTOK_SEMICOLON) || nn_astparser_match(prs, NEON_ASTTOK_NEWLINE))
        {
        }
        return;
    }
    if(nn_astparser_match(prs, NEON_ASTTOK_EOF) || prs->prevtoken.type == NEON_ASTTOK_EOF)
    {
        return;
    }
    /* nn_astparser_consume(prs, NEON_ASTTOK_NEWLINE, "end of statement expected"); */
    while(nn_astparser_match(prs, NEON_ASTTOK_SEMICOLON) || nn_astparser_match(prs, NEON_ASTTOK_NEWLINE))
    {
    }
}

static void nn_astparser_ignorewhitespace(NNAstParser* prs)
{
    while(true)
    {
        if(nn_astparser_check(prs, NEON_ASTTOK_NEWLINE))
        {
            nn_astparser_advance(prs);
        }
        else
        {
            break;
        }
    }
}

static int nn_astparser_getcodeargscount(const NNInstruction* bytecode, const NNValue* constants, int ip)
{
    int constant;
    NNOpCode code;
    NNObjFunction* fn;
    code = (NNOpCode)bytecode[ip].code;
    switch(code)
    {
        case NEON_OP_EQUAL:
        case NEON_OP_PRIMGREATER:
        case NEON_OP_PRIMLESSTHAN:
        case NEON_OP_PUSHNULL:
        case NEON_OP_PUSHTRUE:
        case NEON_OP_PUSHFALSE:
        case NEON_OP_PRIMADD:
        case NEON_OP_PRIMSUBTRACT:
        case NEON_OP_PRIMMULTIPLY:
        case NEON_OP_PRIMDIVIDE:
        case NEON_OP_PRIMFLOORDIVIDE:
        case NEON_OP_PRIMMODULO:
        case NEON_OP_PRIMPOW:
        case NEON_OP_PRIMNEGATE:
        case NEON_OP_PRIMNOT:
        case NEON_OP_ECHO:
        case NEON_OP_TYPEOF:
        case NEON_OP_POPONE:
        case NEON_OP_UPVALUECLOSE:
        case NEON_OP_DUPONE:
        case NEON_OP_RETURN:
        case NEON_OP_CLASSINHERIT:
        case NEON_OP_CLASSGETSUPER:
        case NEON_OP_PRIMAND:
        case NEON_OP_PRIMOR:
        case NEON_OP_PRIMBITXOR:
        case NEON_OP_PRIMSHIFTLEFT:
        case NEON_OP_PRIMSHIFTRIGHT:
        case NEON_OP_PRIMBITNOT:
        case NEON_OP_PUSHONE:
        case NEON_OP_INDEXSET:
        case NEON_OP_ASSERT:
        case NEON_OP_EXTHROW:
        case NEON_OP_EXPOPTRY:
        case NEON_OP_MAKERANGE:
        case NEON_OP_STRINGIFY:
        case NEON_OP_PUSHEMPTY:
        case NEON_OP_EXPUBLISHTRY:
        case NEON_OP_CLASSGETTHIS:
        case NEON_OP_HALT:
            return 0;
        case NEON_OP_CALLFUNCTION:
        case NEON_OP_CLASSINVOKESUPERSELF:
        case NEON_OP_INDEXGET:
        case NEON_OP_INDEXGETRANGED:
            return 1;
        case NEON_OP_GLOBALDEFINE:
        case NEON_OP_GLOBALGET:
        case NEON_OP_GLOBALSET:
        case NEON_OP_LOCALGET:
        case NEON_OP_LOCALSET:
        case NEON_OP_FUNCARGOPTIONAL:
        case NEON_OP_FUNCARGSET:
        case NEON_OP_FUNCARGGET:
        case NEON_OP_UPVALUEGET:
        case NEON_OP_UPVALUESET:
        case NEON_OP_JUMPIFFALSE:
        case NEON_OP_JUMPNOW:
        case NEON_OP_BREAK_PL:
        case NEON_OP_LOOP:
        case NEON_OP_PUSHCONSTANT:
        case NEON_OP_POPN:
        case NEON_OP_MAKECLASS:
        case NEON_OP_PROPERTYGET:
        case NEON_OP_PROPERTYGETSELF:
        case NEON_OP_PROPERTYSET:
        case NEON_OP_MAKEARRAY:
        case NEON_OP_MAKEDICT:
        case NEON_OP_IMPORTIMPORT:
        case NEON_OP_SWITCH:
        case NEON_OP_MAKEMETHOD:
        #if 0
        case NEON_OP_FUNCOPTARG:
        #endif
            return 2;
        case NEON_OP_CALLMETHOD:
        case NEON_OP_CLASSINVOKETHIS:
        case NEON_OP_CLASSINVOKESUPER:
        case NEON_OP_CLASSPROPERTYDEFINE:
            return 3;
        case NEON_OP_EXTRY:
            return 6;
        case NEON_OP_MAKECLOSURE:
            {
                constant = (bytecode[ip + 1].code << 8) | bytecode[ip + 2].code;
                fn = nn_value_asfunction(constants[constant]);
                /* There is two byte for the constant, then three for each up value. */
                return 2 + (fn->upvalcount * 3);
            }
            break;
        default:
            break;
    }
    return 0;
}

static void nn_astemit_emit(NNAstParser* prs, uint8_t byte, int line, bool isop)
{
    NNInstruction ins;
    ins.code = byte;
    ins.srcline = line;
    ins.isop = isop;
    nn_blob_push(nn_astparser_currentblob(prs), ins);
}

static void nn_astemit_patchat(NNAstParser* prs, size_t idx, uint8_t byte)
{
    nn_astparser_currentblob(prs)->instrucs[idx].code = byte;
}

static void nn_astemit_emitinstruc(NNAstParser* prs, uint8_t byte)
{
    nn_astemit_emit(prs, byte, prs->prevtoken.line, true);
}

static void nn_astemit_emit1byte(NNAstParser* prs, uint8_t byte)
{
    nn_astemit_emit(prs, byte, prs->prevtoken.line, false);
}

static void nn_astemit_emit1short(NNAstParser* prs, uint16_t byte)
{
    nn_astemit_emit(prs, (byte >> 8) & 0xff, prs->prevtoken.line, false);
    nn_astemit_emit(prs, byte & 0xff, prs->prevtoken.line, false);
}

static void nn_astemit_emit2byte(NNAstParser* prs, uint8_t byte, uint8_t byte2)
{
    nn_astemit_emit(prs, byte, prs->prevtoken.line, false);
    nn_astemit_emit(prs, byte2, prs->prevtoken.line, false);
}

static void nn_astemit_emitbyteandshort(NNAstParser* prs, uint8_t byte, uint16_t byte2)
{
    nn_astemit_emit(prs, byte, prs->prevtoken.line, false);
    nn_astemit_emit(prs, (byte2 >> 8) & 0xff, prs->prevtoken.line, false);
    nn_astemit_emit(prs, byte2 & 0xff, prs->prevtoken.line, false);
}

static void nn_astemit_emitloop(NNAstParser* prs, int loopstart)
{
    int offset;
    nn_astemit_emitinstruc(prs, NEON_OP_LOOP);
    offset = nn_astparser_currentblob(prs)->count - loopstart + 2;
    if(offset > UINT16_MAX)
    {
        nn_astparser_raiseerror(prs, "loop body too large");
    }
    nn_astemit_emit1byte(prs, (offset >> 8) & 0xff);
    nn_astemit_emit1byte(prs, offset & 0xff);
}

static void nn_astemit_emitreturn(NNAstParser* prs)
{
    if(prs->istrying)
    {
        nn_astemit_emitinstruc(prs, NEON_OP_EXPOPTRY);
    }
    if(prs->currentfunccompiler->contexttype == NEON_FNCONTEXTTYPE_INITIALIZER)
    {
        nn_astemit_emitbyteandshort(prs, NEON_OP_LOCALGET, 0);
    }
    else
    {
        if(!prs->keeplastvalue || prs->lastwasstatement)
        {
            if(prs->currentfunccompiler->fromimport)
            {
                nn_astemit_emitinstruc(prs, NEON_OP_PUSHNULL);
            }
            else
            {
                nn_astemit_emitinstruc(prs, NEON_OP_PUSHEMPTY);
            }
        }
    }
    nn_astemit_emitinstruc(prs, NEON_OP_RETURN);
}

static int nn_astparser_pushconst(NNAstParser* prs, NNValue value)
{
    int constant;
    constant = nn_blob_pushconst(nn_astparser_currentblob(prs), value);
    return constant;
}

static void nn_astemit_emitconst(NNAstParser* prs, NNValue value)
{
    int constant;
    constant = nn_astparser_pushconst(prs, value);
    nn_astemit_emitbyteandshort(prs, NEON_OP_PUSHCONSTANT, (uint16_t)constant);
}

static int nn_astemit_emitjump(NNAstParser* prs, uint8_t instruction)
{
    nn_astemit_emitinstruc(prs, instruction);
    /* placeholders */
    nn_astemit_emit1byte(prs, 0xff);
    nn_astemit_emit1byte(prs, 0xff);
    return nn_astparser_currentblob(prs)->count - 2;
}

static int nn_astemit_emitswitch(NNAstParser* prs)
{
    nn_astemit_emitinstruc(prs, NEON_OP_SWITCH);
    /* placeholders */
    nn_astemit_emit1byte(prs, 0xff);
    nn_astemit_emit1byte(prs, 0xff);
    return nn_astparser_currentblob(prs)->count - 2;
}

static int nn_astemit_emittry(NNAstParser* prs)
{
    nn_astemit_emitinstruc(prs, NEON_OP_EXTRY);
    /* type placeholders */
    nn_astemit_emit1byte(prs, 0xff);
    nn_astemit_emit1byte(prs, 0xff);
    /* handler placeholders */
    nn_astemit_emit1byte(prs, 0xff);
    nn_astemit_emit1byte(prs, 0xff);
    /* finally placeholders */
    nn_astemit_emit1byte(prs, 0xff);
    nn_astemit_emit1byte(prs, 0xff);
    return nn_astparser_currentblob(prs)->count - 6;
}

static void nn_astemit_patchswitch(NNAstParser* prs, int offset, int constant)
{
    nn_astemit_patchat(prs, offset, (constant >> 8) & 0xff);
    nn_astemit_patchat(prs, offset + 1, constant & 0xff);
}

static void nn_astemit_patchtry(NNAstParser* prs, int offset, int type, int address, int finally)
{
    /* patch type */
    nn_astemit_patchat(prs, offset, (type >> 8) & 0xff);
    nn_astemit_patchat(prs, offset + 1, type & 0xff);
    /* patch address */
    nn_astemit_patchat(prs, offset + 2, (address >> 8) & 0xff);
    nn_astemit_patchat(prs, offset + 3, address & 0xff);
    /* patch finally */
    nn_astemit_patchat(prs, offset + 4, (finally >> 8) & 0xff);
    nn_astemit_patchat(prs, offset + 5, finally & 0xff);
}

static void nn_astemit_patchjump(NNAstParser* prs, int offset)
{
    /* -2 to adjust the bytecode for the offset itself */
    int jump;
    jump = nn_astparser_currentblob(prs)->count - offset - 2;
    if(jump > UINT16_MAX)
    {
        nn_astparser_raiseerror(prs, "body of conditional block too large");
    }
    nn_astemit_patchat(prs, offset, (jump >> 8) & 0xff);
    nn_astemit_patchat(prs, offset + 1, jump & 0xff);
}

static void nn_astfunccompiler_init(NNAstParser* prs, NNAstFuncCompiler* fnc, NNFuncContextType type, bool isanon)
{
    bool candeclthis;
    NNIOStream wtmp;
    NNAstLocal* local;
    NNObjString* fname;
    fnc->enclosing = prs->currentfunccompiler;
    fnc->targetfunc = NULL;
    fnc->contexttype = type;
    fnc->localcount = 0;
    fnc->scopedepth = 0;
    fnc->handlercount = 0;
    fnc->fromimport = false;
    fnc->targetfunc = nn_object_makefuncscript(prs->pstate, prs->currentmodule, type);
    prs->currentfunccompiler = fnc;
    if(type != NEON_FNCONTEXTTYPE_SCRIPT)
    {
        nn_vm_stackpush(prs->pstate, nn_value_fromobject(fnc->targetfunc));
        if(isanon)
        {
            nn_iostream_makestackstring(prs->pstate, &wtmp);
            nn_iostream_printf(&wtmp, "anonymous@[%s:%d]", prs->currentfile, prs->prevtoken.line);
            fname = nn_iostream_takestring(&wtmp);
            nn_iostream_destroy(&wtmp);
        }
        else
        {
            fname = nn_string_copylen(prs->pstate, prs->prevtoken.start, prs->prevtoken.length);
        }
        prs->currentfunccompiler->targetfunc->name = fname;
        nn_vm_stackpop(prs->pstate);
    }
    /* claiming slot zero for use in class methods */
    local = &prs->currentfunccompiler->locals[0];
    prs->currentfunccompiler->localcount++;
    local->depth = 0;
    local->iscaptured = false;
    candeclthis = (
        (type != NEON_FNCONTEXTTYPE_FUNCTION) &&
        (prs->compcontext == NEON_COMPCONTEXT_CLASS)
    );
    if(candeclthis || (/*(type == NEON_FNCONTEXTTYPE_ANONYMOUS) &&*/ (prs->compcontext != NEON_COMPCONTEXT_CLASS)))
    {
        local->name.start = g_strthis;
        local->name.length = 4;
    }
    else
    {
        local->name.start = "";
        local->name.length = 0;
    }
}

static int nn_astparser_makeidentconst(NNAstParser* prs, NNAstToken* name)
{
    int rawlen;
    const char* rawstr;
    NNObjString* str;
    rawstr = name->start;
    rawlen = name->length;
    if(name->isglobal)
    {
        rawstr++;
        rawlen--;
    }
    #if 0
    if(strcmp(rawstr, g_strthis))
    {
        
    }
    #endif
    str = nn_string_copylen(prs->pstate, rawstr, rawlen);
    return nn_astparser_pushconst(prs, nn_value_fromobject(str));
}

static bool nn_astparser_identsequal(NNAstToken* a, NNAstToken* b)
{
    return a->length == b->length && memcmp(a->start, b->start, a->length) == 0;
}

static int nn_astfunccompiler_resolvelocal(NNAstParser* prs, NNAstFuncCompiler* fnc, NNAstToken* name)
{
    int i;
    NNAstLocal* local;
    (void)prs;
    for(i = fnc->localcount - 1; i >= 0; i--)
    {
        local = &fnc->locals[i];
        if(nn_astparser_identsequal(&local->name, name))
        {
            #if 0
            if(local->depth == -1)
            {
                nn_astparser_raiseerror(prs, "cannot read local variable in it's own initializer");
            }
            #endif
            return i;
        }
    }
    return -1;
}

static int nn_astfunccompiler_addupvalue(NNAstParser* prs, NNAstFuncCompiler* fnc, uint16_t index, bool islocal)
{
    int i;
    int upcnt;
    NNAstUpvalue* upvalue;
    upcnt = fnc->targetfunc->upvalcount;
    for(i = 0; i < upcnt; i++)
    {
        upvalue = &fnc->upvalues[i];
        if(upvalue->index == index && upvalue->islocal == islocal)
        {
            return i;
        }
    }
    if(upcnt == NEON_CONFIG_ASTMAXUPVALS)
    {
        nn_astparser_raiseerror(prs, "too many closure variables in function");
        return 0;
    }
    fnc->upvalues[upcnt].islocal = islocal;
    fnc->upvalues[upcnt].index = index;
    return fnc->targetfunc->upvalcount++;
}

static int nn_astfunccompiler_resolveupvalue(NNAstParser* prs, NNAstFuncCompiler* fnc, NNAstToken* name)
{
    int local;
    int upvalue;
    if(fnc->enclosing == NULL)
    {
        return -1;
    }
    local = nn_astfunccompiler_resolvelocal(prs, fnc->enclosing, name);
    if(local != -1)
    {
        fnc->enclosing->locals[local].iscaptured = true;
        return nn_astfunccompiler_addupvalue(prs, fnc, (uint16_t)local, true);
    }
    upvalue = nn_astfunccompiler_resolveupvalue(prs, fnc->enclosing, name);
    if(upvalue != -1)
    {
        return nn_astfunccompiler_addupvalue(prs, fnc, (uint16_t)upvalue, false);
    }
    return -1;
}

static int nn_astparser_addlocal(NNAstParser* prs, NNAstToken name)
{
    NNAstLocal* local;
    if(prs->currentfunccompiler->localcount == NEON_CONFIG_ASTMAXLOCALS)
    {
        /* we've reached maximum local variables per scope */
        nn_astparser_raiseerror(prs, "too many local variables in scope");
        return -1;
    }
    local = &prs->currentfunccompiler->locals[prs->currentfunccompiler->localcount++];
    local->name = name;
    local->depth = -1;
    local->iscaptured = false;
    return prs->currentfunccompiler->localcount;
}

static void nn_astparser_declarevariable(NNAstParser* prs)
{
    int i;
    NNAstToken* name;
    NNAstLocal* local;
    /* global variables are implicitly declared... */
    if(prs->currentfunccompiler->scopedepth == 0)
    {
        return;
    }
    name = &prs->prevtoken;
    for(i = prs->currentfunccompiler->localcount - 1; i >= 0; i--)
    {
        local = &prs->currentfunccompiler->locals[i];
        if(local->depth != -1 && local->depth < prs->currentfunccompiler->scopedepth)
        {
            break;
        }
        if(nn_astparser_identsequal(name, &local->name))
        {
            nn_astparser_raiseerror(prs, "%.*s already declared in current scope", name->length, name->start);
        }
    }
    nn_astparser_addlocal(prs, *name);
}

static int nn_astparser_parsevariable(NNAstParser* prs, const char* message)
{
    if(!nn_astparser_consume(prs, NEON_ASTTOK_IDENTNORMAL, message))
    {
        /* what to do here? */
    }
    nn_astparser_declarevariable(prs);
    /* we are in a local scope... */
    if(prs->currentfunccompiler->scopedepth > 0)
    {
        return 0;
    }
    return nn_astparser_makeidentconst(prs, &prs->prevtoken);
}

static void nn_astparser_markinitialized(NNAstParser* prs)
{
    if(prs->currentfunccompiler->scopedepth == 0)
    {
        return;
    }
    prs->currentfunccompiler->locals[prs->currentfunccompiler->localcount - 1].depth = prs->currentfunccompiler->scopedepth;
}

static void nn_astparser_definevariable(NNAstParser* prs, int global)
{
    /* we are in a local scope... */
    if(prs->currentfunccompiler->scopedepth > 0)
    {
        nn_astparser_markinitialized(prs);
        return;
    }
    nn_astemit_emitbyteandshort(prs, NEON_OP_GLOBALDEFINE, global);
}

static NNAstToken nn_astparser_synthtoken(const char* name)
{
    NNAstToken token;
    token.isglobal = false;
    token.line = 0;
    token.type = (NNAstTokType)0;
    token.start = name;
    token.length = (int)strlen(name);
    return token;
}

static NNObjFunction* nn_astparser_endcompiler(NNAstParser* prs, bool istoplevel)
{
    const char* fname;
    NNObjFunction* function;
    nn_astemit_emitreturn(prs);
    if(istoplevel)
    {
    }
    function = prs->currentfunccompiler->targetfunc;
    fname = NULL;
    if(function->name == NULL)
    {
        fname = nn_string_getdata(prs->currentmodule->physicalpath);
    }
    else
    {
        fname = nn_string_getdata(function->name);
    }
    if(!prs->haderror && prs->pstate->conf.dumpbytecode)
    {
        nn_dbg_disasmblob(prs->pstate->debugwriter, nn_astparser_currentblob(prs), fname);
    }
    prs->currentfunccompiler = prs->currentfunccompiler->enclosing;
    return function;
}

static void nn_astparser_scopebegin(NNAstParser* prs)
{
    prs->currentfunccompiler->scopedepth++;
}

static bool nn_astutil_scopeendcancontinue(NNAstParser* prs)
{
    int lopos;
    int locount;
    int lodepth;
    int scodepth;
    locount = prs->currentfunccompiler->localcount;
    lopos = prs->currentfunccompiler->localcount - 1;
    lodepth = prs->currentfunccompiler->locals[lopos].depth;
    scodepth = prs->currentfunccompiler->scopedepth;
    if(locount > 0 && lodepth > scodepth)
    {
        return true;
    }
    return false;
}

static void nn_astparser_scopeend(NNAstParser* prs)
{
    prs->currentfunccompiler->scopedepth--;
    /*
    // remove all variables declared in scope while exiting...
    */
    if(prs->keeplastvalue)
    {
        #if 0
            return;
        #endif
    }
    while(nn_astutil_scopeendcancontinue(prs))
    {
        if(prs->currentfunccompiler->locals[prs->currentfunccompiler->localcount - 1].iscaptured)
        {
            nn_astemit_emitinstruc(prs, NEON_OP_UPVALUECLOSE);
        }
        else
        {
            nn_astemit_emitinstruc(prs, NEON_OP_POPONE);
        }
        prs->currentfunccompiler->localcount--;
    }
}

static int nn_astparser_discardlocals(NNAstParser* prs, int depth)
{
    int local;
    if(prs->keeplastvalue)
    {
        #if 0
            return 0;
        #endif
    }
    if(prs->currentfunccompiler->scopedepth == -1)
    {
        nn_astparser_raiseerror(prs, "cannot exit top-level scope");
    }
    local = prs->currentfunccompiler->localcount - 1;
    while(local >= 0 && prs->currentfunccompiler->locals[local].depth >= depth)
    {
        if(prs->currentfunccompiler->locals[local].iscaptured)
        {
            nn_astemit_emitinstruc(prs, NEON_OP_UPVALUECLOSE);
        }
        else
        {
            nn_astemit_emitinstruc(prs, NEON_OP_POPONE);
        }
        local--;
    }
    return prs->currentfunccompiler->localcount - local - 1;
}

static void nn_astparser_endloop(NNAstParser* prs)
{
    int i;
    NNInstruction* bcode;
    NNValue* cvals;
    /*
    // find all NEON_OP_BREAK_PL placeholder and replace with the appropriate jump...
    */
    i = prs->innermostloopstart;
    while(i < prs->currentfunccompiler->targetfunc->fnscriptfunc.blob.count)
    {
        if(prs->currentfunccompiler->targetfunc->fnscriptfunc.blob.instrucs[i].code == NEON_OP_BREAK_PL)
        {
            prs->currentfunccompiler->targetfunc->fnscriptfunc.blob.instrucs[i].code = NEON_OP_JUMPNOW;
            nn_astemit_patchjump(prs, i + 1);
            i += 3;
        }
        else
        {
            bcode = prs->currentfunccompiler->targetfunc->fnscriptfunc.blob.instrucs;
            cvals = prs->currentfunccompiler->targetfunc->fnscriptfunc.blob.constants.listitems;
            i += 1 + nn_astparser_getcodeargscount(bcode, cvals, i);
        }
    }
}

static bool nn_astparser_rulebinary(NNAstParser* prs, NNAstToken previous, bool canassign)
{
    NNAstTokType op;
    NNAstRule* rule;
    (void)previous;
    (void)canassign;
    op = prs->prevtoken.type;
    /* compile the right operand */
    rule = nn_astparser_getrule(op);
    nn_astparser_parseprecedence(prs, (NNAstPrecedence)(rule->precedence + 1));
    /* emit the operator instruction */
    switch(op)
    {
        case NEON_ASTTOK_PLUS:
            nn_astemit_emitinstruc(prs, NEON_OP_PRIMADD);
            break;
        case NEON_ASTTOK_MINUS:
            nn_astemit_emitinstruc(prs, NEON_OP_PRIMSUBTRACT);
            break;
        case NEON_ASTTOK_MULTIPLY:
            nn_astemit_emitinstruc(prs, NEON_OP_PRIMMULTIPLY);
            break;
        case NEON_ASTTOK_DIVIDE:
            nn_astemit_emitinstruc(prs, NEON_OP_PRIMDIVIDE);
            break;
        case NEON_ASTTOK_MODULO:
            nn_astemit_emitinstruc(prs, NEON_OP_PRIMMODULO);
            break;
        case NEON_ASTTOK_POWEROF:
            nn_astemit_emitinstruc(prs, NEON_OP_PRIMPOW);
            break;
        case NEON_ASTTOK_FLOOR:
            nn_astemit_emitinstruc(prs, NEON_OP_PRIMFLOORDIVIDE);
            break;
            /* equality */
        case NEON_ASTTOK_EQUAL:
            nn_astemit_emitinstruc(prs, NEON_OP_EQUAL);
            break;
        case NEON_ASTTOK_NOTEQUAL:
            nn_astemit_emit2byte(prs, NEON_OP_EQUAL, NEON_OP_PRIMNOT);
            break;
        case NEON_ASTTOK_GREATERTHAN:
            nn_astemit_emitinstruc(prs, NEON_OP_PRIMGREATER);
            break;
        case NEON_ASTTOK_GREATER_EQ:
            nn_astemit_emit2byte(prs, NEON_OP_PRIMLESSTHAN, NEON_OP_PRIMNOT);
            break;
        case NEON_ASTTOK_LESSTHAN:
            nn_astemit_emitinstruc(prs, NEON_OP_PRIMLESSTHAN);
            break;
        case NEON_ASTTOK_LESSEQUAL:
            nn_astemit_emit2byte(prs, NEON_OP_PRIMGREATER, NEON_OP_PRIMNOT);
            break;
            /* bitwise */
        case NEON_ASTTOK_AMP:
            nn_astemit_emitinstruc(prs, NEON_OP_PRIMAND);
            break;
        case NEON_ASTTOK_BAR:
            nn_astemit_emitinstruc(prs, NEON_OP_PRIMOR);
            break;
        case NEON_ASTTOK_XOR:
            nn_astemit_emitinstruc(prs, NEON_OP_PRIMBITXOR);
            break;
        case NEON_ASTTOK_LEFTSHIFT:
            nn_astemit_emitinstruc(prs, NEON_OP_PRIMSHIFTLEFT);
            break;
        case NEON_ASTTOK_RIGHTSHIFT:
            nn_astemit_emitinstruc(prs, NEON_OP_PRIMSHIFTRIGHT);
            break;
            /* range */
        case NEON_ASTTOK_DOUBLEDOT:
            nn_astemit_emitinstruc(prs, NEON_OP_MAKERANGE);
            break;
        default:
            break;
    }
    return true;
}

static bool nn_astparser_rulecall(NNAstParser* prs, NNAstToken previous, bool canassign)
{
    uint8_t argcount;
    (void)previous;
    (void)canassign;
    argcount = nn_astparser_parsefunccallargs(prs);
    nn_astemit_emit2byte(prs, NEON_OP_CALLFUNCTION, argcount);
    return true;
}

static bool nn_astparser_ruleliteral(NNAstParser* prs, bool canassign)
{
    (void)canassign;
    switch(prs->prevtoken.type)
    {
        case NEON_ASTTOK_KWNULL:
            nn_astemit_emitinstruc(prs, NEON_OP_PUSHNULL);
            break;
        case NEON_ASTTOK_KWTRUE:
            nn_astemit_emitinstruc(prs, NEON_OP_PUSHTRUE);
            break;
        case NEON_ASTTOK_KWFALSE:
            nn_astemit_emitinstruc(prs, NEON_OP_PUSHFALSE);
            break;
        default:
            /* TODO: assuming this is correct behaviour ... */
            return false;
    }
    return true;
}

static void nn_astparser_parseassign(NNAstParser* prs, uint8_t realop, uint8_t getop, uint8_t setop, int arg)
{
    prs->replcanecho = false;
    if(getop == NEON_OP_PROPERTYGET || getop == NEON_OP_PROPERTYGETSELF)
    {
        nn_astemit_emitinstruc(prs, NEON_OP_DUPONE);
    }
    if(arg != -1)
    {
        nn_astemit_emitbyteandshort(prs, getop, arg);
    }
    else
    {
        nn_astemit_emit2byte(prs, getop, 1);
    }
    nn_astparser_parseexpression(prs);
    nn_astemit_emitinstruc(prs, realop);
    if(arg != -1)
    {
        nn_astemit_emitbyteandshort(prs, setop, (uint16_t)arg);
    }
    else
    {
        nn_astemit_emitinstruc(prs, setop);
    }
}

static void nn_astparser_assignment(NNAstParser* prs, uint8_t getop, uint8_t setop, int arg, bool canassign)
{
    if(canassign && nn_astparser_match(prs, NEON_ASTTOK_ASSIGN))
    {
        prs->replcanecho = false;
        nn_astparser_parseexpression(prs);
        if(arg != -1)
        {
            nn_astemit_emitbyteandshort(prs, setop, (uint16_t)arg);
        }
        else
        {
            nn_astemit_emitinstruc(prs, setop);
        }
    }
    else if(canassign && nn_astparser_match(prs, NEON_ASTTOK_PLUSASSIGN))
    {
        nn_astparser_parseassign(prs, NEON_OP_PRIMADD, getop, setop, arg);
    }
    else if(canassign && nn_astparser_match(prs, NEON_ASTTOK_MINUSASSIGN))
    {
        nn_astparser_parseassign(prs, NEON_OP_PRIMSUBTRACT, getop, setop, arg);
    }
    else if(canassign && nn_astparser_match(prs, NEON_ASTTOK_MULTASSIGN))
    {
        nn_astparser_parseassign(prs, NEON_OP_PRIMMULTIPLY, getop, setop, arg);
    }
    else if(canassign && nn_astparser_match(prs, NEON_ASTTOK_DIVASSIGN))
    {
        nn_astparser_parseassign(prs, NEON_OP_PRIMDIVIDE, getop, setop, arg);
    }
    else if(canassign && nn_astparser_match(prs, NEON_ASTTOK_POWASSIGN))
    {
        nn_astparser_parseassign(prs, NEON_OP_PRIMPOW, getop, setop, arg);
    }
    else if(canassign && nn_astparser_match(prs, NEON_ASTTOK_PERCENT_EQ))
    {
        nn_astparser_parseassign(prs, NEON_OP_PRIMMODULO, getop, setop, arg);
    }
    else if(canassign && nn_astparser_match(prs, NEON_ASTTOK_AMP_EQ))
    {
        nn_astparser_parseassign(prs, NEON_OP_PRIMAND, getop, setop, arg);
    }
    else if(canassign && nn_astparser_match(prs, NEON_ASTTOK_BAR_EQ))
    {
        nn_astparser_parseassign(prs, NEON_OP_PRIMOR, getop, setop, arg);
    }
    else if(canassign && nn_astparser_match(prs, NEON_ASTTOK_TILDE_EQ))
    {
        nn_astparser_parseassign(prs, NEON_OP_PRIMBITNOT, getop, setop, arg);
    }
    else if(canassign && nn_astparser_match(prs, NEON_ASTTOK_XOR_EQ))
    {
        nn_astparser_parseassign(prs, NEON_OP_PRIMBITXOR, getop, setop, arg);
    }
    else if(canassign && nn_astparser_match(prs, NEON_ASTTOK_LEFTSHIFTASSIGN))
    {
        nn_astparser_parseassign(prs, NEON_OP_PRIMSHIFTLEFT, getop, setop, arg);
    }
    else if(canassign && nn_astparser_match(prs, NEON_ASTTOK_RIGHTSHIFTASSIGN))
    {
        nn_astparser_parseassign(prs, NEON_OP_PRIMSHIFTRIGHT, getop, setop, arg);
    }
    else if(canassign && nn_astparser_match(prs, NEON_ASTTOK_INCREMENT))
    {
        prs->replcanecho = false;
        if(getop == NEON_OP_PROPERTYGET || getop == NEON_OP_PROPERTYGETSELF)
        {
            nn_astemit_emitinstruc(prs, NEON_OP_DUPONE);
        }
        if(arg != -1)
        {
            nn_astemit_emitbyteandshort(prs, getop, arg);
        }
        else
        {
            nn_astemit_emit2byte(prs, getop, 1);
        }
        nn_astemit_emit2byte(prs, NEON_OP_PUSHONE, NEON_OP_PRIMADD);
        nn_astemit_emitbyteandshort(prs, setop, (uint16_t)arg);
    }
    else if(canassign && nn_astparser_match(prs, NEON_ASTTOK_DECREMENT))
    {
        prs->replcanecho = false;
        if(getop == NEON_OP_PROPERTYGET || getop == NEON_OP_PROPERTYGETSELF)
        {
            nn_astemit_emitinstruc(prs, NEON_OP_DUPONE);
        }

        if(arg != -1)
        {
            nn_astemit_emitbyteandshort(prs, getop, arg);
        }
        else
        {
            nn_astemit_emit2byte(prs, getop, 1);
        }

        nn_astemit_emit2byte(prs, NEON_OP_PUSHONE, NEON_OP_PRIMSUBTRACT);
        nn_astemit_emitbyteandshort(prs, setop, (uint16_t)arg);
    }
    else
    {
        if(arg != -1)
        {
            if(getop == NEON_OP_INDEXGET || getop == NEON_OP_INDEXGETRANGED)
            {
                nn_astemit_emit2byte(prs, getop, (uint8_t)0);
            }
            else
            {
                nn_astemit_emitbyteandshort(prs, getop, (uint16_t)arg);
            }
        }
        else
        {
            nn_astemit_emit2byte(prs, getop, (uint8_t)0);
        }
    }
}

static bool nn_astparser_ruledot(NNAstParser* prs, NNAstToken previous, bool canassign)
{
    int name;
    bool caninvoke;
    uint8_t argcount;
    NNOpCode getop;
    NNOpCode setop;
    nn_astparser_ignorewhitespace(prs);
    if(!nn_astparser_consume(prs, NEON_ASTTOK_IDENTNORMAL, "expected property name after '.'"))
    {
        return false;
    }
    name = nn_astparser_makeidentconst(prs, &prs->prevtoken);
    if(nn_astparser_match(prs, NEON_ASTTOK_PARENOPEN))
    {
        argcount = nn_astparser_parsefunccallargs(prs);
        caninvoke = (
            (prs->currentclasscompiler != NULL) &&
            (
                (previous.type == NEON_ASTTOK_KWTHIS) ||
                (nn_astparser_identsequal(&prs->prevtoken, &prs->currentclasscompiler->name))
            )
        );
        if(caninvoke)
        {
            nn_astemit_emitbyteandshort(prs, NEON_OP_CLASSINVOKETHIS, name);
        }
        else
        {
            nn_astemit_emitbyteandshort(prs, NEON_OP_CALLMETHOD, name);
        }
        nn_astemit_emit1byte(prs, argcount);
    }
    else
    {
        getop = NEON_OP_PROPERTYGET;
        setop = NEON_OP_PROPERTYSET;
        if(prs->currentclasscompiler != NULL && (previous.type == NEON_ASTTOK_KWTHIS || nn_astparser_identsequal(&prs->prevtoken, &prs->currentclasscompiler->name)))
        {
            getop = NEON_OP_PROPERTYGETSELF;
        }
        nn_astparser_assignment(prs, getop, setop, name, canassign);
    }
    return true;
}

static void nn_astparser_namedvar(NNAstParser* prs, NNAstToken name, bool canassign)
{
    bool fromclass;
    uint8_t getop;
    uint8_t setop;
    int arg;
    (void)fromclass;
    fromclass = prs->currentclasscompiler != NULL;
    arg = nn_astfunccompiler_resolvelocal(prs, prs->currentfunccompiler, &name);
    if(arg != -1)
    {
        if(prs->infunction)
        {
            getop = NEON_OP_FUNCARGGET;
            setop = NEON_OP_FUNCARGSET;
        }
        else
        {
            getop = NEON_OP_LOCALGET;
            setop = NEON_OP_LOCALSET;
        }
    }
    else
    {
        arg = nn_astfunccompiler_resolveupvalue(prs, prs->currentfunccompiler, &name);
        if((arg != -1) && (name.isglobal == false))
        {
            getop = NEON_OP_UPVALUEGET;
            setop = NEON_OP_UPVALUESET;
        }
        else
        {
            arg = nn_astparser_makeidentconst(prs, &name);
            getop = NEON_OP_GLOBALGET;
            setop = NEON_OP_GLOBALSET;
        }
    }
    nn_astparser_assignment(prs, getop, setop, arg, canassign);
}

static void nn_astparser_createdvar(NNAstParser* prs, NNAstToken name)
{
    int local;
    if(prs->currentfunccompiler->targetfunc->name != NULL)
    {
        local = nn_astparser_addlocal(prs, name) - 1;
        nn_astparser_markinitialized(prs);
        nn_astemit_emitbyteandshort(prs, NEON_OP_LOCALSET, (uint16_t)local);
    }
    else
    {
        nn_astemit_emitbyteandshort(prs, NEON_OP_GLOBALDEFINE, (uint16_t)nn_astparser_makeidentconst(prs, &name));
    }
}

static bool nn_astparser_rulearray(NNAstParser* prs, bool canassign)
{
    int count;
    (void)canassign;
    /* placeholder for the list */
    nn_astemit_emitinstruc(prs, NEON_OP_PUSHNULL);
    count = 0;
    nn_astparser_ignorewhitespace(prs);
    if(!nn_astparser_check(prs, NEON_ASTTOK_BRACKETCLOSE))
    {
        do
        {
            nn_astparser_ignorewhitespace(prs);
            if(!nn_astparser_check(prs, NEON_ASTTOK_BRACKETCLOSE))
            {
                /* allow comma to end lists */
                nn_astparser_parseexpression(prs);
                nn_astparser_ignorewhitespace(prs);
                count++;
            }
            nn_astparser_ignorewhitespace(prs);
        } while(nn_astparser_match(prs, NEON_ASTTOK_COMMA));
    }
    nn_astparser_ignorewhitespace(prs);
    nn_astparser_consume(prs, NEON_ASTTOK_BRACKETCLOSE, "expected ']' at end of list");
    nn_astemit_emitbyteandshort(prs, NEON_OP_MAKEARRAY, count);
    return true;
}

static bool nn_astparser_ruledictionary(NNAstParser* prs, bool canassign)
{
    bool usedexpression;
    int itemcount;
    (void)canassign;
    /* placeholder for the dictionary */
    nn_astemit_emitinstruc(prs, NEON_OP_PUSHNULL);
    itemcount = 0;
    nn_astparser_ignorewhitespace(prs);
    if(!nn_astparser_check(prs, NEON_ASTTOK_BRACECLOSE))
    {
        do
        {
            nn_astparser_ignorewhitespace(prs);
            if(!nn_astparser_check(prs, NEON_ASTTOK_BRACECLOSE))
            {
                /* allow last pair to end with a comma */
                usedexpression = false;
                if(nn_astparser_check(prs, NEON_ASTTOK_IDENTNORMAL))
                {
                    nn_astparser_consume(prs, NEON_ASTTOK_IDENTNORMAL, "");
                    nn_astemit_emitconst(prs, nn_value_fromobject(nn_string_copylen(prs->pstate, prs->prevtoken.start, prs->prevtoken.length)));
                }
                else
                {
                    nn_astparser_parseexpression(prs);
                    usedexpression = true;
                }
                nn_astparser_ignorewhitespace(prs);
                if(!nn_astparser_check(prs, NEON_ASTTOK_COMMA) && !nn_astparser_check(prs, NEON_ASTTOK_BRACECLOSE))
                {
                    nn_astparser_consume(prs, NEON_ASTTOK_COLON, "expected ':' after dictionary key");
                    nn_astparser_ignorewhitespace(prs);

                    nn_astparser_parseexpression(prs);
                }
                else
                {
                    if(usedexpression)
                    {
                        nn_astparser_raiseerror(prs, "cannot infer dictionary values from expressions");
                        return false;
                    }
                    else
                    {
                        nn_astparser_namedvar(prs, prs->prevtoken, false);
                    }
                }
                itemcount++;
            }
        } while(nn_astparser_match(prs, NEON_ASTTOK_COMMA));
    }
    nn_astparser_ignorewhitespace(prs);
    nn_astparser_consume(prs, NEON_ASTTOK_BRACECLOSE, "expected '}' after dictionary");
    nn_astemit_emitbyteandshort(prs, NEON_OP_MAKEDICT, itemcount);
    return true;
}

static bool nn_astparser_ruleindexing(NNAstParser* prs, NNAstToken previous, bool canassign)
{
    bool assignable;
    bool commamatch;
    uint8_t getop;
    (void)previous;
    (void)canassign;
    assignable = true;
    commamatch = false;
    getop = NEON_OP_INDEXGET;
    if(nn_astparser_match(prs, NEON_ASTTOK_COMMA))
    {
        nn_astemit_emitinstruc(prs, NEON_OP_PUSHNULL);
        commamatch = true;
        getop = NEON_OP_INDEXGETRANGED;
    }
    else
    {
        nn_astparser_parseexpression(prs);
    }
    if(!nn_astparser_match(prs, NEON_ASTTOK_BRACKETCLOSE))
    {
        getop = NEON_OP_INDEXGETRANGED;
        if(!commamatch)
        {
            nn_astparser_consume(prs, NEON_ASTTOK_COMMA, "expecting ',' or ']'");
        }
        if(nn_astparser_match(prs, NEON_ASTTOK_BRACKETCLOSE))
        {
            nn_astemit_emitinstruc(prs, NEON_OP_PUSHNULL);
        }
        else
        {
            nn_astparser_parseexpression(prs);
            nn_astparser_consume(prs, NEON_ASTTOK_BRACKETCLOSE, "expected ']' after indexing");
        }
        assignable = false;
    }
    else
    {
        if(commamatch)
        {
            nn_astemit_emitinstruc(prs, NEON_OP_PUSHNULL);
        }
    }
    nn_astparser_assignment(prs, getop, NEON_OP_INDEXSET, -1, assignable);
    return true;
}

static bool nn_astparser_rulevarnormal(NNAstParser* prs, bool canassign)
{
    nn_astparser_namedvar(prs, prs->prevtoken, canassign);
    return true;
}


static bool nn_astparser_rulethis(NNAstParser* prs, bool canassign)
{
    (void)canassign;
    #if 0
    if(prs->currentclasscompiler == NULL)
    {
        nn_astparser_raiseerror(prs, "cannot use keyword 'this' outside of a class");
        return false;
    }
    #endif
    #if 0
    if(prs->currentclasscompiler != NULL)
    #endif
    {
        nn_astparser_namedvar(prs, prs->prevtoken, false);
        #if 0
            nn_astparser_namedvar(prs, nn_astparser_synthtoken(g_strthis), false);
        #endif
    }
    #if 0
        nn_astemit_emitinstruc(prs, NEON_OP_CLASSGETTHIS);
    #endif
    return true;
}

static bool nn_astparser_rulesuper(NNAstParser* prs, bool canassign)
{
    int name;
    bool invokeself;
    uint8_t argcount;
    (void)canassign;
    if(prs->currentclasscompiler == NULL)
    {
        nn_astparser_raiseerror(prs, "cannot use keyword 'super' outside of a class");
        return false;
    }
    else if(!prs->currentclasscompiler->hassuperclass)
    {
        nn_astparser_raiseerror(prs, "cannot use keyword 'super' in a class without a superclass");
        return false;
    }
    name = -1;
    invokeself = false;
    if(!nn_astparser_check(prs, NEON_ASTTOK_PARENOPEN))
    {
        nn_astparser_consume(prs, NEON_ASTTOK_DOT, "expected '.' or '(' after super");
        nn_astparser_consume(prs, NEON_ASTTOK_IDENTNORMAL, "expected super class method name after .");
        name = nn_astparser_makeidentconst(prs, &prs->prevtoken);
    }
    else
    {
        invokeself = true;
    }
    nn_astparser_namedvar(prs, nn_astparser_synthtoken(g_strthis), false);
    if(nn_astparser_match(prs, NEON_ASTTOK_PARENOPEN))
    {
        argcount = nn_astparser_parsefunccallargs(prs);
        nn_astparser_namedvar(prs, nn_astparser_synthtoken(g_strsuper), false);
        if(!invokeself)
        {
            nn_astemit_emitbyteandshort(prs, NEON_OP_CLASSINVOKESUPER, name);
            nn_astemit_emit1byte(prs, argcount);
        }
        else
        {
            nn_astemit_emit2byte(prs, NEON_OP_CLASSINVOKESUPERSELF, argcount);
        }
    }
    else
    {
        nn_astparser_namedvar(prs, nn_astparser_synthtoken(g_strsuper), false);
        nn_astemit_emitbyteandshort(prs, NEON_OP_CLASSGETSUPER, name);
    }
    return true;
}

static bool nn_astparser_rulegrouping(NNAstParser* prs, bool canassign)
{
    (void)canassign;
    nn_astparser_ignorewhitespace(prs);
    nn_astparser_parseexpression(prs);
    while(nn_astparser_match(prs, NEON_ASTTOK_COMMA))
    {
        nn_astparser_parseexpression(prs);
    }
    nn_astparser_ignorewhitespace(prs);
    nn_astparser_consume(prs, NEON_ASTTOK_PARENCLOSE, "expected ')' after grouped expression");
    return true;
}

NNValue nn_astparser_compilestrnumber(NNAstTokType type, const char* source)
{
    double dbval;
    long longval;
    int64_t llval;
    if(type == NEON_ASTTOK_LITNUMBIN)
    {
        llval = strtoll(source + 2, NULL, 2);
        return nn_value_makenumber(llval);
    }
    else if(type == NEON_ASTTOK_LITNUMOCT)
    {
        longval = strtol(source + 2, NULL, 8);
        return nn_value_makenumber(longval);
    }
    else if(type == NEON_ASTTOK_LITNUMHEX)
    {
        longval = strtol(source, NULL, 16);
        return nn_value_makenumber(longval);
    }
    dbval = strtod(source, NULL);
    return nn_value_makenumber(dbval);
}

static NNValue nn_astparser_compilenumber(NNAstParser* prs)
{
    return nn_astparser_compilestrnumber(prs->prevtoken.type, prs->prevtoken.start);
}

static bool nn_astparser_rulenumber(NNAstParser* prs, bool canassign)
{
    (void)canassign;
    nn_astemit_emitconst(prs, nn_astparser_compilenumber(prs));
    return true;
}

/*
// Reads the next character, which should be a hex digit (0-9, a-f, or A-F) and
// returns its numeric value. If the character isn't a hex digit, returns -1.
*/
static int nn_astparser_readhexdigit(char c)
{
    if((c >= '0') && (c <= '9'))
    {
        return (c - '0');
    }
    if((c >= 'a') && (c <= 'f'))
    {
        return ((c - 'a') + 10);
    }
    if((c >= 'A') && (c <= 'F'))
    {
        return ((c - 'A') + 10);
    }
    return -1;
}

/*
// Reads [digits] hex digits in a string literal and returns their number value.
*/
static int nn_astparser_readhexescape(NNAstParser* prs, const char* str, int index, int count)
{
    size_t pos;
    int i;
    int cval;
    int digit;
    int value;
    value = 0;
    i = 0;
    digit = 0;
    for(; i < count; i++)
    {
        pos = (index + i + 2);
        cval = str[pos];
        digit = nn_astparser_readhexdigit(cval);
        if(digit == -1)
        {
            nn_astparser_raiseerror(prs, "invalid hex escape sequence at #%d of \"%s\": '%c' (%d)", pos, str, cval, cval);
        }
        value = (value * 16) | digit;
    }
    if(count == 4 && (digit = nn_astparser_readhexdigit(str[index + i + 2])) != -1)
    {
        value = (value * 16) | digit;
    }
    return value;
}

static int nn_astparser_readunicodeescape(NNAstParser* prs, char* string, const char* realstring, int numberbytes, int realindex, int index)
{
    int value;
    int count;
    size_t len;
    char* chr;
    value = nn_astparser_readhexescape(prs, realstring, realindex, numberbytes);
    count = nn_util_utf8numbytes(value);
    if(count == -1)
    {
        nn_astparser_raiseerror(prs, "cannot encode a negative unicode value");
    }
    /* check for greater that \uffff */
    if(value > 65535)
    {
        count++;
    }
    if(count != 0)
    {
        chr = nn_util_utf8encode(value, &len);
        if(chr)
        {
            memcpy(string + index, chr, (size_t)count + 1);
            nn_memory_free(chr);
        }
        else
        {
            nn_astparser_raiseerror(prs, "cannot decode unicode escape at index %d", realindex);
        }
    }
    /* but greater than \uffff doesn't occupy any extra byte */
    /*
    if(value > 65535)
    {
        count--;
    }
    */
    return count;
}

static char* nn_astparser_compilestring(NNAstParser* prs, int* length, bool permitescapes)
{
    int k;
    int i;
    int count;
    int reallength;
    int rawlen;
    char c;
    char quote;
    char* deststr;
    char* realstr;
    rawlen = (((size_t)prs->prevtoken.length - 2) + 1);
    deststr = (char*)nn_memory_malloc(sizeof(char) * rawlen);
    quote = prs->prevtoken.start[0];
    realstr = (char*)prs->prevtoken.start + 1;
    reallength = prs->prevtoken.length - 2;
    k = 0;
    for(i = 0; i < reallength; i++, k++)
    {
        c = realstr[i];
        if(permitescapes)
        {
            if(c == '\\' && i < reallength - 1)
            {
                switch(realstr[i + 1])
                {
                    case '0':
                        {
                            c = '\0';
                        }
                        break;
                    case '$':
                        {
                            c = '$';
                        }
                        break;
                    case '\'':
                        {
                            if(quote == '\'' || quote == '}')
                            {
                                /* } handle closing of interpolation. */
                                c = '\'';
                            }
                            else
                            {
                                i--;
                            }
                        }
                        break;
                    case '"':
                        {
                            if(quote == '"' || quote == '}')
                            {
                                c = '"';
                            }
                            else
                            {
                                i--;
                            }
                        }
                        break;
                    case 'a':
                        {
                            c = '\a';
                        }
                        break;
                    case 'b':
                        {
                            c = '\b';
                        }
                        break;
                    case 'f':
                        {
                            c = '\f';
                        }
                        break;
                    case 'n':
                        {
                            c = '\n';
                        }
                        break;
                    case 'r':
                        {
                            c = '\r';
                        }
                        break;
                    case 't':
                        {
                            c = '\t';
                        }
                        break;
                    case 'e':
                        {
                            c = 27;
                        }
                        break;
                    case '\\':
                        {
                            c = '\\';
                        }
                        break;
                    case 'v':
                        {
                            c = '\v';
                        }
                        break;
                    case 'x':
                        {
                            #if 0
                                int nn_astparser_readunicodeescape(NNAstParser* prs, char* string, char* realstring, int numberbytes, int realindex, int index)
                                int nn_astparser_readhexescape(NNAstParser* prs, const char* str, int index, int count)
                                k += nn_astparser_readunicodeescape(prs, deststr, realstr, 2, i, k) - 1;
                                k += nn_astparser_readhexescape(prs, deststr, i, 2) - 0;
                            #endif
                            c = nn_astparser_readhexescape(prs, realstr, i, 2) - 0;
                            i += 2;
                            #if 0
                                continue;
                            #endif
                        }
                        break;
                    case 'u':
                        {
                            count = nn_astparser_readunicodeescape(prs, deststr, realstr, 4, i, k);
                            if(count > 4)
                            {
                                k += count - 2;
                            }
                            else
                            {
                                k += count - 1;
                            }
                            if(count > 4)
                            {
                                i += 6;
                            }
                            else
                            {
                                i += 5;
                            }
                            continue;
                        }
                    case 'U':
                        {
                            count = nn_astparser_readunicodeescape(prs, deststr, realstr, 8, i, k);
                            if(count > 4)
                            {
                                k += count - 2;
                            }
                            else
                            {
                                k += count - 1;
                            }
                            i += 9;
                            continue;
                        }
                    default:
                        {
                            i--;
                        }
                        break;
                }
                i++;
            }
        }
        memcpy(deststr + k, &c, 1);
    }
    *length = k;
    deststr[k] = '\0';
    return deststr;
}

static bool nn_astparser_rulestring(NNAstParser* prs, bool canassign)
{
    int length;
    char* str;
    (void)canassign;
    str = nn_astparser_compilestring(prs, &length, true);
    nn_astemit_emitconst(prs, nn_value_fromobject(nn_string_takelen(prs->pstate, str, length)));
    return true;
}

static bool nn_astparser_rulerawstring(NNAstParser* prs, bool canassign)
{
    int length;
    char* str;
    (void)canassign;
    str = nn_astparser_compilestring(prs, &length, false);
    nn_astemit_emitconst(prs, nn_value_fromobject(nn_string_takelen(prs->pstate, str, length)));
    return true;
}

static bool nn_astparser_ruleinterpolstring(NNAstParser* prs, bool canassign)
{
    int count;
    bool doadd;
    bool stringmatched;
    count = 0;
    do
    {
        doadd = false;
        stringmatched = false;
        if(prs->prevtoken.length - 2 > 0)
        {
            nn_astparser_rulestring(prs, canassign);
            doadd = true;
            stringmatched = true;
            if(count > 0)
            {
                nn_astemit_emitinstruc(prs, NEON_OP_PRIMADD);
            }
        }
        nn_astparser_parseexpression(prs);
        nn_astemit_emitinstruc(prs, NEON_OP_STRINGIFY);
        if(doadd || (count >= 1 && stringmatched == false))
        {
            nn_astemit_emitinstruc(prs, NEON_OP_PRIMADD);
        }
        count++;
    } while(nn_astparser_match(prs, NEON_ASTTOK_INTERPOLATION));
    nn_astparser_consume(prs, NEON_ASTTOK_LITERALSTRING, "unterminated string interpolation");
    if(prs->prevtoken.length - 2 > 0)
    {
        nn_astparser_rulestring(prs, canassign);
        nn_astemit_emitinstruc(prs, NEON_OP_PRIMADD);
    }
    return true;
}

static bool nn_astparser_ruleunary(NNAstParser* prs, bool canassign)
{
    NNAstTokType op;
    (void)canassign;
    op = prs->prevtoken.type;
    /* compile the expression */
    nn_astparser_parseprecedence(prs, NEON_ASTPREC_UNARY);
    /* emit instruction */
    switch(op)
    {
        case NEON_ASTTOK_MINUS:
            nn_astemit_emitinstruc(prs, NEON_OP_PRIMNEGATE);
            break;
        case NEON_ASTTOK_EXCLMARK:
            nn_astemit_emitinstruc(prs, NEON_OP_PRIMNOT);
            break;
        case NEON_ASTTOK_TILDE:
            nn_astemit_emitinstruc(prs, NEON_OP_PRIMBITNOT);
            break;
        default:
            break;
    }
    return true;
}

static bool nn_astparser_ruleand(NNAstParser* prs, NNAstToken previous, bool canassign)
{
    int endjump;
    (void)previous;
    (void)canassign;
    endjump = nn_astemit_emitjump(prs, NEON_OP_JUMPIFFALSE);
    nn_astemit_emitinstruc(prs, NEON_OP_POPONE);
    nn_astparser_parseprecedence(prs, NEON_ASTPREC_AND);
    nn_astemit_patchjump(prs, endjump);
    return true;
}


static bool nn_astparser_ruleor(NNAstParser* prs, NNAstToken previous, bool canassign)
{
    int endjump;
    int elsejump;
    (void)previous;
    (void)canassign;
    elsejump = nn_astemit_emitjump(prs, NEON_OP_JUMPIFFALSE);
    endjump = nn_astemit_emitjump(prs, NEON_OP_JUMPNOW);
    nn_astemit_patchjump(prs, elsejump);
    nn_astemit_emitinstruc(prs, NEON_OP_POPONE);
    nn_astparser_parseprecedence(prs, NEON_ASTPREC_OR);
    nn_astemit_patchjump(prs, endjump);
    return true;
}

static bool nn_astparser_ruleinstanceof(NNAstParser* prs, NNAstToken previous, bool canassign)
{
    (void)previous;
    (void)canassign;
    nn_astparser_parseexpression(prs);
    nn_astemit_emitinstruc(prs, NEON_OP_OPINSTANCEOF);

    return true;
}

static bool nn_astparser_ruleconditional(NNAstParser* prs, NNAstToken previous, bool canassign)
{
    int thenjump;
    int elsejump;
    (void)previous;
    (void)canassign;
    thenjump = nn_astemit_emitjump(prs, NEON_OP_JUMPIFFALSE);
    nn_astemit_emitinstruc(prs, NEON_OP_POPONE);
    nn_astparser_ignorewhitespace(prs);
    /* compile the then expression */
    nn_astparser_parseprecedence(prs, NEON_ASTPREC_CONDITIONAL);
    nn_astparser_ignorewhitespace(prs);
    elsejump = nn_astemit_emitjump(prs, NEON_OP_JUMPNOW);
    nn_astemit_patchjump(prs, thenjump);
    nn_astemit_emitinstruc(prs, NEON_OP_POPONE);
    nn_astparser_consume(prs, NEON_ASTTOK_COLON, "expected matching ':' after '?' conditional");
    nn_astparser_ignorewhitespace(prs);
    /*
    // compile the else expression
    // here we parse at NEON_ASTPREC_ASSIGNMENT precedence as
    // linear conditionals can be nested.
    */
    nn_astparser_parseprecedence(prs, NEON_ASTPREC_ASSIGNMENT);
    nn_astemit_patchjump(prs, elsejump);
    return true;
}

static bool nn_astparser_ruleimport(NNAstParser* prs, bool canassign)
{
    (void)canassign;
    nn_astparser_parseexpression(prs);
    nn_astemit_emitinstruc(prs, NEON_OP_IMPORTIMPORT);
    return true;
}

static bool nn_astparser_rulenew(NNAstParser* prs, bool canassign)
{
    nn_astparser_consume(prs, NEON_ASTTOK_IDENTNORMAL, "class name after 'new'");
    return nn_astparser_rulevarnormal(prs, canassign);
}

static bool nn_astparser_ruletypeof(NNAstParser* prs, bool canassign)
{
    (void)canassign;
    nn_astparser_consume(prs, NEON_ASTTOK_PARENOPEN, "expected '(' after 'typeof'");
    nn_astparser_parseexpression(prs);
    nn_astparser_consume(prs, NEON_ASTTOK_PARENCLOSE, "expected ')' after 'typeof'");
    nn_astemit_emitinstruc(prs, NEON_OP_TYPEOF);
    return true;
}

static bool nn_astparser_rulenothingprefix(NNAstParser* prs, bool canassign)
{
    (void)prs;
    (void)canassign;
    return true;
}

static bool nn_astparser_rulenothinginfix(NNAstParser* prs, NNAstToken previous, bool canassign)
{
    (void)prs;
    (void)previous;
    (void)canassign;
    return true;
}

static NNAstRule* nn_astparser_putrule(NNAstRule* dest, NNAstParsePrefixFN prefix, NNAstParseInfixFN infix, NNAstPrecedence precedence)
{
    dest->prefix = prefix;
    dest->infix = infix;
    dest->precedence = precedence;
    return dest;
}

#define dorule(tok, prefix, infix, precedence) \
    case tok: return nn_astparser_putrule(&dest, prefix, infix, precedence);

NNAstRule* nn_astparser_getrule(NNAstTokType type)
{
    static NNAstRule dest;
    switch(type)
    {
        dorule(NEON_ASTTOK_NEWLINE, nn_astparser_rulenothingprefix, nn_astparser_rulenothinginfix, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_PARENOPEN, nn_astparser_rulegrouping, nn_astparser_rulecall, NEON_ASTPREC_CALL );
        dorule(NEON_ASTTOK_PARENCLOSE, NULL, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_BRACKETOPEN, nn_astparser_rulearray, nn_astparser_ruleindexing, NEON_ASTPREC_CALL );
        dorule(NEON_ASTTOK_BRACKETCLOSE, NULL, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_BRACEOPEN, nn_astparser_ruledictionary, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_BRACECLOSE, NULL, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_SEMICOLON, nn_astparser_rulenothingprefix, nn_astparser_rulenothinginfix, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_COMMA, NULL, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_BACKSLASH, NULL, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_EXCLMARK, nn_astparser_ruleunary, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_NOTEQUAL, NULL, nn_astparser_rulebinary, NEON_ASTPREC_EQUALITY );
        dorule(NEON_ASTTOK_COLON, NULL, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_AT, nn_astparser_ruleanonfunc, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_DOT, NULL, nn_astparser_ruledot, NEON_ASTPREC_CALL );
        dorule(NEON_ASTTOK_DOUBLEDOT, NULL, nn_astparser_rulebinary, NEON_ASTPREC_RANGE );
        dorule(NEON_ASTTOK_TRIPLEDOT, NULL, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_PLUS, nn_astparser_ruleunary, nn_astparser_rulebinary, NEON_ASTPREC_TERM );
        dorule(NEON_ASTTOK_PLUSASSIGN, NULL, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_INCREMENT, NULL, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_MINUS, nn_astparser_ruleunary, nn_astparser_rulebinary, NEON_ASTPREC_TERM );
        dorule(NEON_ASTTOK_MINUSASSIGN, NULL, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_DECREMENT, NULL, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_MULTIPLY, NULL, nn_astparser_rulebinary, NEON_ASTPREC_FACTOR );
        dorule(NEON_ASTTOK_MULTASSIGN, NULL, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_POWEROF, NULL, nn_astparser_rulebinary, NEON_ASTPREC_FACTOR );
        dorule(NEON_ASTTOK_POWASSIGN, NULL, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_DIVIDE, NULL, nn_astparser_rulebinary, NEON_ASTPREC_FACTOR );
        dorule(NEON_ASTTOK_DIVASSIGN, NULL, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_FLOOR, NULL, nn_astparser_rulebinary, NEON_ASTPREC_FACTOR );
        dorule(NEON_ASTTOK_ASSIGN, NULL, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_EQUAL, NULL, nn_astparser_rulebinary, NEON_ASTPREC_EQUALITY );
        dorule(NEON_ASTTOK_LESSTHAN, NULL, nn_astparser_rulebinary, NEON_ASTPREC_COMPARISON );
        dorule(NEON_ASTTOK_LESSEQUAL, NULL, nn_astparser_rulebinary, NEON_ASTPREC_COMPARISON );
        dorule(NEON_ASTTOK_LEFTSHIFT, NULL, nn_astparser_rulebinary, NEON_ASTPREC_SHIFT );
        dorule(NEON_ASTTOK_LEFTSHIFTASSIGN, NULL, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_GREATERTHAN, NULL, nn_astparser_rulebinary, NEON_ASTPREC_COMPARISON );
        dorule(NEON_ASTTOK_GREATER_EQ, NULL, nn_astparser_rulebinary, NEON_ASTPREC_COMPARISON );
        dorule(NEON_ASTTOK_RIGHTSHIFT, NULL, nn_astparser_rulebinary, NEON_ASTPREC_SHIFT );
        dorule(NEON_ASTTOK_RIGHTSHIFTASSIGN, NULL, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_MODULO, NULL, nn_astparser_rulebinary, NEON_ASTPREC_FACTOR );
        dorule(NEON_ASTTOK_PERCENT_EQ, NULL, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_AMP, NULL, nn_astparser_rulebinary, NEON_ASTPREC_BITAND );
        dorule(NEON_ASTTOK_AMP_EQ, NULL, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_BAR, /*nn_astparser_ruleanoncompat*/ NULL, nn_astparser_rulebinary, NEON_ASTPREC_BITOR );
        dorule(NEON_ASTTOK_BAR_EQ, NULL, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_TILDE, nn_astparser_ruleunary, NULL, NEON_ASTPREC_UNARY );
        dorule(NEON_ASTTOK_TILDE_EQ, NULL, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_XOR, NULL, nn_astparser_rulebinary, NEON_ASTPREC_BITXOR );
        dorule(NEON_ASTTOK_XOR_EQ, NULL, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_QUESTION, NULL, nn_astparser_ruleconditional, NEON_ASTPREC_CONDITIONAL );
        dorule(NEON_ASTTOK_KWAND, NULL, nn_astparser_ruleand, NEON_ASTPREC_AND );
        dorule(NEON_ASTTOK_KWAS, NULL, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_KWASSERT, NULL, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_KWBREAK, NULL, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_KWCLASS, nn_astparser_ruleanonclass, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_KWCONTINUE, NULL, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_KWFUNCTION, nn_astparser_ruleanonfunc, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_KWDEFAULT, NULL, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_KWTHROW, NULL, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_KWDO, NULL, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_KWECHO, NULL, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_KWELSE, NULL, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_KWFALSE, nn_astparser_ruleliteral, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_KWFOREACH, NULL, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_KWIF, NULL, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_KWIMPORT, nn_astparser_ruleimport, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_KWIN, NULL, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_KWINSTANCEOF, NULL, nn_astparser_ruleinstanceof, NEON_ASTPREC_OR );
        dorule(NEON_ASTTOK_KWFOR, NULL, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_KWVAR, NULL, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_KWNULL, nn_astparser_ruleliteral, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_KWNEW, nn_astparser_rulenew, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_KWTYPEOF, nn_astparser_ruletypeof, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_KWOR, NULL, nn_astparser_ruleor, NEON_ASTPREC_OR );
        dorule(NEON_ASTTOK_KWSUPER, nn_astparser_rulesuper, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_KWRETURN, NULL, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_KWTHIS, nn_astparser_rulethis, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_KWSTATIC, NULL, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_KWTRUE, nn_astparser_ruleliteral, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_KWSWITCH, NULL, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_KWCASE, NULL, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_KWWHILE, NULL, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_KWTRY, NULL, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_KWCATCH, NULL, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_KWFINALLY, NULL, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_LITERALSTRING, nn_astparser_rulestring, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_LITERALRAWSTRING, nn_astparser_rulerawstring, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_LITNUMREG, nn_astparser_rulenumber, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_LITNUMBIN, nn_astparser_rulenumber, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_LITNUMOCT, nn_astparser_rulenumber, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_LITNUMHEX, nn_astparser_rulenumber, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_IDENTNORMAL, nn_astparser_rulevarnormal, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_INTERPOLATION, nn_astparser_ruleinterpolstring, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_EOF, NULL, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_ERROR, NULL, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_KWEMPTY, nn_astparser_ruleliteral, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_UNDEFINED, NULL, NULL, NEON_ASTPREC_NONE );
        default:
            fprintf(stderr, "missing rule?\n");
            break;
    }
    return NULL;
}
#undef dorule

static bool nn_astparser_doparseprecedence(NNAstParser* prs, NNAstPrecedence precedence/*, NNAstExpression* dest*/)
{
    bool canassign;
    NNAstRule* rule;
    NNAstToken previous;
    NNAstParseInfixFN infixrule;
    NNAstParsePrefixFN prefixrule;
    rule = nn_astparser_getrule(prs->prevtoken.type);
    if(rule == NULL)
    {
        return false;
    }
    prefixrule = rule->prefix;
    if(prefixrule == NULL)
    {
        nn_astparser_raiseerror(prs, "expected expression");
        return false;
    }
    canassign = precedence <= NEON_ASTPREC_ASSIGNMENT;
    prefixrule(prs, canassign);
    while(true)
    {
        rule = nn_astparser_getrule(prs->currtoken.type);
        if(rule == NULL)
        {
            return false;
        }
        if(precedence <= rule->precedence)
        {
            previous = prs->prevtoken;
            nn_astparser_ignorewhitespace(prs);
            nn_astparser_advance(prs);
            infixrule = nn_astparser_getrule(prs->prevtoken.type)->infix;
            infixrule(prs, previous, canassign);
        }
        else
        {
            break;
        }
    }
    if(canassign && nn_astparser_match(prs, NEON_ASTTOK_ASSIGN))
    {
        nn_astparser_raiseerror(prs, "invalid assignment target");
        return false;
    }
    return true;
}

static bool nn_astparser_parseprecedence(NNAstParser* prs, NNAstPrecedence precedence)
{
    if(nn_astlex_isatend(prs->lexer) && prs->pstate->isrepl)
    {
        return false;
    }
    nn_astparser_ignorewhitespace(prs);
    if(nn_astlex_isatend(prs->lexer) && prs->pstate->isrepl)
    {
        return false;
    }
    nn_astparser_advance(prs);
    return nn_astparser_doparseprecedence(prs, precedence);
}

static bool nn_astparser_parseprecnoadvance(NNAstParser* prs, NNAstPrecedence precedence)
{
    if(nn_astlex_isatend(prs->lexer) && prs->pstate->isrepl)
    {
        return false;
    }
    nn_astparser_ignorewhitespace(prs);
    if(nn_astlex_isatend(prs->lexer) && prs->pstate->isrepl)
    {
        return false;
    }
    return nn_astparser_doparseprecedence(prs, precedence);
}

static bool nn_astparser_parseexpression(NNAstParser* prs)
{
    return nn_astparser_parseprecedence(prs, NEON_ASTPREC_ASSIGNMENT);
}

static bool nn_astparser_parseblock(NNAstParser* prs)
{
    prs->blockcount++;
    nn_astparser_ignorewhitespace(prs);
    while(!nn_astparser_check(prs, NEON_ASTTOK_BRACECLOSE) && !nn_astparser_check(prs, NEON_ASTTOK_EOF))
    {
        nn_astparser_parsedeclaration(prs);
    }
    prs->blockcount--;
    if(!nn_astparser_consume(prs, NEON_ASTTOK_BRACECLOSE, "expected '}' after block"))
    {
        return false;
    }
    if(nn_astparser_match(prs, NEON_ASTTOK_SEMICOLON))
    {
    }
    return true;
}

static void nn_astparser_declarefuncargvar(NNAstParser* prs)
{
    int i;
    NNAstToken* name;
    NNAstLocal* local;
    /* global variables are implicitly declared... */
    if(prs->currentfunccompiler->scopedepth == 0)
    {
        return;
    }
    name = &prs->prevtoken;
    for(i = prs->currentfunccompiler->localcount - 1; i >= 0; i--)
    {
        local = &prs->currentfunccompiler->locals[i];
        if(local->depth != -1 && local->depth < prs->currentfunccompiler->scopedepth)
        {
            break;
        }
        if(nn_astparser_identsequal(name, &local->name))
        {
            nn_astparser_raiseerror(prs, "%.*s already declared in current scope", name->length, name->start);
        }
    }
    nn_astparser_addlocal(prs, *name);
}


static int nn_astparser_parsefuncparamvar(NNAstParser* prs, const char* message)
{
    if(!nn_astparser_consume(prs, NEON_ASTTOK_IDENTNORMAL, message))
    {
        /* what to do here? */
    }
    nn_astparser_declarefuncargvar(prs);
    /* we are in a local scope... */
    if(prs->currentfunccompiler->scopedepth > 0)
    {
        return 0;
    }
    return nn_astparser_makeidentconst(prs, &prs->prevtoken);
}

static uint8_t nn_astparser_parsefunccallargs(NNAstParser* prs)
{
    uint8_t argcount;
    argcount = 0;
    if(!nn_astparser_check(prs, NEON_ASTTOK_PARENCLOSE))
    {
        do
        {
            nn_astparser_ignorewhitespace(prs);
            nn_astparser_parseexpression(prs);
            if(argcount == NEON_CONFIG_ASTMAXFUNCPARAMS)
            {
                nn_astparser_raiseerror(prs, "cannot have more than %d arguments to a function", NEON_CONFIG_ASTMAXFUNCPARAMS);
            }
            argcount++;
        } while(nn_astparser_match(prs, NEON_ASTTOK_COMMA));
    }
    nn_astparser_ignorewhitespace(prs);
    if(!nn_astparser_consume(prs, NEON_ASTTOK_PARENCLOSE, "expected ')' after argument list"))
    {
        /* TODO: handle this, somehow. */
    }
    return argcount;
}

static void nn_astparser_parsefuncparamlist(NNAstParser* prs, NNAstFuncCompiler* fnc)
{
    int defvalconst;
    int paramconst;
    size_t paramid;
    NNAstToken paramname;
    NNAstToken vargname;
    (void)paramid;
    (void)paramname;
    (void)defvalconst;
    (void)fnc;
    paramid = 0;
    /* compile argument list... */
    do
    {
        nn_astparser_ignorewhitespace(prs);
        prs->currentfunccompiler->targetfunc->fnscriptfunc.arity++;
        if(nn_astparser_match(prs, NEON_ASTTOK_TRIPLEDOT))
        {
            prs->currentfunccompiler->targetfunc->fnscriptfunc.isvariadic = true;
            nn_astparser_consume(prs, NEON_ASTTOK_IDENTNORMAL, "expected identifier after '...'");
            vargname = prs->prevtoken;
            nn_astparser_addlocal(prs, vargname);
            nn_astparser_definevariable(prs, 0);
            break;
        }
        paramconst = nn_astparser_parsefuncparamvar(prs, "expected parameter name");
        paramname = prs->prevtoken;
        nn_astparser_definevariable(prs, paramconst);
        nn_astparser_ignorewhitespace(prs);
        #if 1
        if(nn_astparser_match(prs, NEON_ASTTOK_ASSIGN))
        {
            fprintf(stderr, "parsing optional argument....\n");
            if(!nn_astparser_parseexpression(prs))
            {
                nn_astparser_raiseerror(prs, "failed to parse function default paramter value");
            }
            #if 0
                defvalconst = nn_astparser_addlocal(prs, paramname);
            #else
                defvalconst = paramconst;
            #endif
            #if 1
                #if 1
                    nn_astemit_emitbyteandshort(prs, NEON_OP_FUNCARGOPTIONAL, defvalconst);
                    //nn_astemit_emit1short(prs, paramid);
                #else
                    nn_astemit_emitbyteandshort(prs, NEON_OP_LOCALSET, defvalconst);
                #endif
            #endif
        }
        #endif
        nn_astparser_ignorewhitespace(prs);
        paramid++;

    } while(nn_astparser_match(prs, NEON_ASTTOK_COMMA));
}

static void nn_astfunccompiler_compilebody(NNAstParser* prs, NNAstFuncCompiler* fnc, bool closescope, bool isanon)
{
    int i;
    NNObjFunction* function;
    (void)isanon;
    /* compile the body */
    nn_astparser_ignorewhitespace(prs);
    nn_astparser_consume(prs, NEON_ASTTOK_BRACEOPEN, "expected '{' before function body");
    nn_astparser_parseblock(prs);
    /* create the function object */
    if(closescope)
    {
        nn_astparser_scopeend(prs);
    }
    function = nn_astparser_endcompiler(prs, false);
    nn_vm_stackpush(prs->pstate, nn_value_fromobject(function));
    nn_astemit_emitbyteandshort(prs, NEON_OP_MAKECLOSURE, nn_astparser_pushconst(prs, nn_value_fromobject(function)));
    for(i = 0; i < function->upvalcount; i++)
    {
        nn_astemit_emit1byte(prs, fnc->upvalues[i].islocal ? 1 : 0);
        nn_astemit_emit1short(prs, fnc->upvalues[i].index);
    }
    nn_vm_stackpop(prs->pstate);
}

static void nn_astparser_parsefuncfull(NNAstParser* prs, NNFuncContextType type, bool isanon)
{
    NNAstFuncCompiler fnc;
    prs->infunction = true;
    nn_astfunccompiler_init(prs, &fnc, type, isanon);
    nn_astparser_scopebegin(prs);
    /* compile parameter list */
    nn_astparser_consume(prs, NEON_ASTTOK_PARENOPEN, "expected '(' after function name");
    if(!nn_astparser_check(prs, NEON_ASTTOK_PARENCLOSE))
    {
        nn_astparser_parsefuncparamlist(prs, &fnc);
    }
    nn_astparser_consume(prs, NEON_ASTTOK_PARENCLOSE, "expected ')' after function parameters");
    nn_astfunccompiler_compilebody(prs, &fnc, false, isanon);
    prs->infunction = false;
}

static void nn_astparser_parsemethod(NNAstParser* prs, NNAstToken classname, NNAstToken methodname, bool havenametoken, bool isstatic)
{
    size_t sn;
    int constant;
    const char* sc;
    NNFuncContextType type;
    NNAstToken actualmthname;
    static NNAstTokType tkns[] = { NEON_ASTTOK_IDENTNORMAL, NEON_ASTTOK_DECORATOR };
    (void)classname;
    sc = "constructor";
    sn = strlen(sc);
    if(havenametoken)
    {
        actualmthname = methodname;
    }
    else
    {
        nn_astparser_consumeor(prs, "method name expected", tkns, 2);
        actualmthname = prs->prevtoken;
    }
    constant = nn_astparser_makeidentconst(prs, &actualmthname);
    type = NEON_FNCONTEXTTYPE_METHOD;
    if(isstatic)
    {
        type = NEON_FNCONTEXTTYPE_STATIC;
    }
    if((prs->prevtoken.length == (int)sn) && (memcmp(prs->prevtoken.start, sc, sn) == 0))
    {
        type = NEON_FNCONTEXTTYPE_INITIALIZER;
    }
    else if((prs->prevtoken.length > 0) && (prs->prevtoken.start[0] == '_'))
    {
        type = NEON_FNCONTEXTTYPE_PRIVATE;
    }
    nn_astparser_parsefuncfull(prs, type, false);
    nn_astemit_emitbyteandshort(prs, NEON_OP_MAKEMETHOD, constant);
}

static bool nn_astparser_ruleanonfunc(NNAstParser* prs, bool canassign)
{
    NNAstFuncCompiler fnc;
    (void)canassign;
    nn_astfunccompiler_init(prs, &fnc, NEON_FNCONTEXTTYPE_FUNCTION, true);
    nn_astparser_scopebegin(prs);
    /* compile parameter list */
    if(nn_astparser_check(prs, NEON_ASTTOK_IDENTNORMAL))
    {
        nn_astparser_consume(prs, NEON_ASTTOK_IDENTNORMAL, "optional name for anonymous function");
    }
    nn_astparser_consume(prs, NEON_ASTTOK_PARENOPEN, "expected '(' at start of anonymous function");
    if(!nn_astparser_check(prs, NEON_ASTTOK_PARENCLOSE))
    {
        nn_astparser_parsefuncparamlist(prs, &fnc);
    }
    nn_astparser_consume(prs, NEON_ASTTOK_PARENCLOSE, "expected ')' after anonymous function parameters");
    nn_astfunccompiler_compilebody(prs, &fnc, true, true);
    return true;
}


static bool nn_astparser_ruleanonclass(NNAstParser* prs, bool canassign)
{
    (void)canassign;
    nn_astparser_parseclassdeclaration(prs, false);
    return true;
}

static bool nn_astparser_parsefield(NNAstParser* prs, NNAstToken* nametokendest, bool* havenamedest, bool isstatic)
{
    int fieldconstant;
    NNAstToken fieldname;
    *havenamedest = false;
    if(nn_astparser_match(prs, NEON_ASTTOK_IDENTNORMAL))
    {
        fieldname = prs->prevtoken;
        *nametokendest = fieldname;
        if(nn_astparser_check(prs, NEON_ASTTOK_ASSIGN))
        {
            nn_astparser_consume(prs, NEON_ASTTOK_ASSIGN, "expected '=' after ident");
            fieldconstant = nn_astparser_makeidentconst(prs, &fieldname);
            nn_astparser_parseexpression(prs);
            nn_astemit_emitbyteandshort(prs, NEON_OP_CLASSPROPERTYDEFINE, fieldconstant);
            nn_astemit_emit1byte(prs, isstatic ? 1 : 0);
            nn_astparser_consumestmtend(prs);
            nn_astparser_ignorewhitespace(prs);
            return true;
        }
    }
    *havenamedest = true;
    return false;
}

static void nn_astparser_parsefuncdecl(NNAstParser* prs)
{
    int global;
    global = nn_astparser_parsevariable(prs, "function name expected");
    nn_astparser_markinitialized(prs);
    nn_astparser_parsefuncfull(prs, NEON_FNCONTEXTTYPE_FUNCTION, false);
    nn_astparser_definevariable(prs, global);
}

static void nn_astparser_parseclassdeclaration(NNAstParser* prs, bool named)
{
    bool isstatic;
    bool havenametoken;
    int nameconst;
    NNAstToken nametoken;
    NNAstCompContext oldctx;
    NNAstToken classname;
    NNAstClassCompiler classcompiler;
    /*
                ClassCompiler classcompiler;
                classcompiler.hasname = named;
                if(named)
                {
                    consume(Token::TOK_IDENTNORMAL, "class name expected");
                    classname = m_prevtoken;
                    declareVariable();
                }
                else
                {
                    classname = makeSynthToken("<anonclass>");
                }
                nameconst = makeIdentConst(&classname);
    */
    if(named)
    {
        nn_astparser_consume(prs, NEON_ASTTOK_IDENTNORMAL, "class name expected");
        classname = prs->prevtoken;
        nn_astparser_declarevariable(prs);
    }
    else
    {
        classname = nn_astparser_synthtoken("<anonclass>");
    }
    nameconst = nn_astparser_makeidentconst(prs, &classname);
    nn_astemit_emitbyteandshort(prs, NEON_OP_MAKECLASS, nameconst);
    if(named)
    {
        nn_astparser_definevariable(prs, nameconst);
    }
    classcompiler.name = prs->prevtoken;
    classcompiler.hassuperclass = false;
    classcompiler.enclosing = prs->currentclasscompiler;
    prs->currentclasscompiler = &classcompiler;
    oldctx = prs->compcontext;
    prs->compcontext = NEON_COMPCONTEXT_CLASS;
    if(nn_astparser_match(prs, NEON_ASTTOK_KWEXTENDS))
    {
        nn_astparser_consume(prs, NEON_ASTTOK_IDENTNORMAL, "name of superclass expected");
        nn_astparser_rulevarnormal(prs, false);
        if(nn_astparser_identsequal(&classname, &prs->prevtoken))
        {
            nn_astparser_raiseerror(prs, "class %.*s cannot inherit from itself", classname.length, classname.start);
        }
        nn_astparser_scopebegin(prs);
        nn_astparser_addlocal(prs, nn_astparser_synthtoken(g_strsuper));
        nn_astparser_definevariable(prs, 0);
        nn_astparser_namedvar(prs, classname, false);
        nn_astemit_emitinstruc(prs, NEON_OP_CLASSINHERIT);
        classcompiler.hassuperclass = true;
    }
    if(named)
    {
        nn_astparser_namedvar(prs, classname, false);
    }
    nn_astparser_ignorewhitespace(prs);
    nn_astparser_consume(prs, NEON_ASTTOK_BRACEOPEN, "expected '{' before class body");
    nn_astparser_ignorewhitespace(prs);
    while(!nn_astparser_check(prs, NEON_ASTTOK_BRACECLOSE) && !nn_astparser_check(prs, NEON_ASTTOK_EOF))
    {
        isstatic = false;
        if(nn_astparser_match(prs, NEON_ASTTOK_KWSTATIC))
        {
            isstatic = true;
        }
        if(nn_astparser_match(prs, NEON_ASTTOK_KWVAR))
        {
            /*
            * TODO:
            * using 'var ... =' in a class is actually semantically superfluous,
            * but not incorrect either. maybe warn that this syntax is deprecated?
            */
        }
        if(!nn_astparser_parsefield(prs, &nametoken, &havenametoken, isstatic))
        {
            nn_astparser_parsemethod(prs, classname, nametoken, havenametoken, isstatic);
        }
        nn_astparser_ignorewhitespace(prs);
    }
    nn_astparser_consume(prs, NEON_ASTTOK_BRACECLOSE, "expected '}' after class body");
    if(nn_astparser_match(prs, NEON_ASTTOK_SEMICOLON))
    {
    }
    if(named)
    {
        nn_astemit_emitinstruc(prs, NEON_OP_POPONE);
    }
    if(classcompiler.hassuperclass)
    {
        nn_astparser_scopeend(prs);
    }
    prs->currentclasscompiler = prs->currentclasscompiler->enclosing;
    prs->compcontext = oldctx;
}

static void nn_astparser_parsevardecl(NNAstParser* prs, bool isinitializer, bool isconst)
{
    int global;
    int totalparsed;
    (void)isconst;
    totalparsed = 0;
    do
    {
        if(totalparsed > 0)
        {
            nn_astparser_ignorewhitespace(prs);
        }
        global = nn_astparser_parsevariable(prs, "variable name expected");
        if(nn_astparser_match(prs, NEON_ASTTOK_ASSIGN))
        {
            nn_astparser_parseexpression(prs);
        }
        else
        {
            nn_astemit_emitinstruc(prs, NEON_OP_PUSHNULL);
        }
        nn_astparser_definevariable(prs, global);
        totalparsed++;
    } while(nn_astparser_match(prs, NEON_ASTTOK_COMMA));
    if(!isinitializer)
    {
        nn_astparser_consumestmtend(prs);
    }
    else
    {
        nn_astparser_consume(prs, NEON_ASTTOK_SEMICOLON, "expected ';' after initializer");
        nn_astparser_ignorewhitespace(prs);
    }
}

static void nn_astparser_parseexprstmt(NNAstParser* prs, bool isinitializer, bool semi)
{
    if(prs->pstate->isrepl && prs->currentfunccompiler->scopedepth == 0)
    {
        prs->replcanecho = true;
    }
    if(!semi)
    {
        nn_astparser_parseexpression(prs);
    }
    else
    {
        nn_astparser_parseprecnoadvance(prs, NEON_ASTPREC_ASSIGNMENT);
    }
    if(!isinitializer)
    {
        if(prs->replcanecho && prs->pstate->isrepl)
        {
            nn_astemit_emitinstruc(prs, NEON_OP_ECHO);
            prs->replcanecho = false;
        }
        else
        {
            #if 0
            if(!prs->keeplastvalue)
            #endif
            {
                nn_astemit_emitinstruc(prs, NEON_OP_POPONE);
            }
        }
        nn_astparser_consumestmtend(prs);
    }
    else
    {
        nn_astparser_consume(prs, NEON_ASTTOK_SEMICOLON, "expected ';' after initializer");
        nn_astparser_ignorewhitespace(prs);
        nn_astemit_emitinstruc(prs, NEON_OP_POPONE);
    }
}

/**
 * iter statements are like for loops in c...
 * they are desugared into a while loop
 *
 * i.e.
 *
 * iter i = 0; i < 10; i++ {
 *    ...
 * }
 *
 * desugars into:
 *
 * var i = 0
 * while i < 10 {
 *    ...
 *    i = i + 1
 * }
 */
 
static void nn_astparser_parseforstmt(NNAstParser* prs)
{
    int exitjump;
    int bodyjump;
    int incrstart;
    int surroundingloopstart;
    int surroundingscopedepth;
    nn_astparser_scopebegin(prs);
    nn_astparser_consume(prs, NEON_ASTTOK_PARENOPEN, "expected '(' after 'for'");
    /* parse initializer... */
    if(nn_astparser_match(prs, NEON_ASTTOK_SEMICOLON))
    {
        /* no initializer */
    }
    else if(nn_astparser_match(prs, NEON_ASTTOK_KWVAR))
    {
        nn_astparser_parsevardecl(prs, true, false);
    }
    else
    {
        nn_astparser_parseexprstmt(prs, true, false);
    }
    /* keep a copy of the surrounding loop's start and depth */
    surroundingloopstart = prs->innermostloopstart;
    surroundingscopedepth = prs->innermostloopscopedepth;
    /* update the parser's loop start and depth to the current */
    prs->innermostloopstart = nn_astparser_currentblob(prs)->count;
    prs->innermostloopscopedepth = prs->currentfunccompiler->scopedepth;
    exitjump = -1;
    if(!nn_astparser_match(prs, NEON_ASTTOK_SEMICOLON))
    {
        /* the condition is optional */
        nn_astparser_parseexpression(prs);
        nn_astparser_consume(prs, NEON_ASTTOK_SEMICOLON, "expected ';' after condition");
        nn_astparser_ignorewhitespace(prs);
        /* jump out of the loop if the condition is false... */
        exitjump = nn_astemit_emitjump(prs, NEON_OP_JUMPIFFALSE);
        nn_astemit_emitinstruc(prs, NEON_OP_POPONE);
        /* pop the condition */
    }
    /* the iterator... */
    if(!nn_astparser_check(prs, NEON_ASTTOK_BRACEOPEN))
    {
        bodyjump = nn_astemit_emitjump(prs, NEON_OP_JUMPNOW);
        incrstart = nn_astparser_currentblob(prs)->count;
        nn_astparser_parseexpression(prs);
        nn_astparser_ignorewhitespace(prs);
        nn_astemit_emitinstruc(prs, NEON_OP_POPONE);
        nn_astemit_emitloop(prs, prs->innermostloopstart);
        prs->innermostloopstart = incrstart;
        nn_astemit_patchjump(prs, bodyjump);
    }
    nn_astparser_consume(prs, NEON_ASTTOK_PARENCLOSE, "expected ')' after 'for'");
    nn_astparser_parsestmt(prs);
    nn_astemit_emitloop(prs, prs->innermostloopstart);
    if(exitjump != -1)
    {
        nn_astemit_patchjump(prs, exitjump);
        nn_astemit_emitinstruc(prs, NEON_OP_POPONE);
    }
    nn_astparser_endloop(prs);
    /* reset the loop start and scope depth to the surrounding value */
    prs->innermostloopstart = surroundingloopstart;
    prs->innermostloopscopedepth = surroundingscopedepth;
    nn_astparser_scopeend(prs);
}

/**
 * for x in iterable {
 *    ...
 * }
 *
 * ==
 *
 * {
 *    var iterable = expression()
 *    var _
 *
 *    while _ = iterable.@itern() {
 *      var x = iterable.@iter()
 *      ...
 *    }
 * }
 *
 * ---------------------------------
 *
 * foreach x, y in iterable {
 *    ...
 * }
 *
 * ==
 *
 * {
 *    var iterable = expression()
 *    var x
 *
 *    while x = iterable.@itern() {
 *      var y = iterable.@iter()
 *      ...
 *    }
 * }
 *
 * Every iterable Object must implement the @iter(x) and the @itern(x)
 * function.
 *
 * to make instances of a user created class iterable,
 * the class must implement the @iter(x) and the @itern(x) function.
 * the @itern(x) must return the current iterating index of the object and
 * the
 * @iter(x) function must return the value at that index.
 * _NOTE_: the @iter(x) function will no longer be called after the
 * @itern(x) function returns a false value. so the @iter(x) never needs
 * to return a false value
 */
static void nn_astparser_parseforeachstmt(NNAstParser* prs)
{
    int citer;
    int citern;
    int falsejump;
    int keyslot;
    int valueslot;
    int iteratorslot;
    int surroundingloopstart;
    int surroundingscopedepth;
    NNAstToken iteratortoken;
    NNAstToken keytoken;
    NNAstToken valuetoken;
    nn_astparser_scopebegin(prs);
    /* define @iter and @itern constant */
    citer = nn_astparser_pushconst(prs, nn_value_fromobject(nn_string_intern(prs->pstate, "@iter")));
    citern = nn_astparser_pushconst(prs, nn_value_fromobject(nn_string_intern(prs->pstate, "@itern")));
    nn_astparser_consume(prs, NEON_ASTTOK_PARENOPEN, "expected '(' after 'foreach'");
    nn_astparser_consume(prs, NEON_ASTTOK_IDENTNORMAL, "expected variable name after 'foreach'");
    if(!nn_astparser_check(prs, NEON_ASTTOK_COMMA))
    {
        keytoken = nn_astparser_synthtoken(" _ ");
        valuetoken = prs->prevtoken;
    }
    else
    {
        keytoken = prs->prevtoken;
        nn_astparser_consume(prs, NEON_ASTTOK_COMMA, "");
        nn_astparser_consume(prs, NEON_ASTTOK_IDENTNORMAL, "expected variable name after ','");
        valuetoken = prs->prevtoken;
    }
    nn_astparser_consume(prs, NEON_ASTTOK_KWIN, "expected 'in' after for loop variable(s)");
    nn_astparser_ignorewhitespace(prs);
    /*
    // The space in the variable name ensures it won't collide with a user-defined
    // variable.
    */
    iteratortoken = nn_astparser_synthtoken(" iterator ");
    /* Evaluate the sequence expression and store it in a hidden local variable. */
    nn_astparser_parseexpression(prs);
    nn_astparser_consume(prs, NEON_ASTTOK_PARENCLOSE, "expected ')' after 'foreach'");
    if(prs->currentfunccompiler->localcount + 3 > NEON_CONFIG_ASTMAXLOCALS)
    {
        nn_astparser_raiseerror(prs, "cannot declare more than %d variables in one scope", NEON_CONFIG_ASTMAXLOCALS);
        return;
    }
    /* add the iterator to the local scope */
    iteratorslot = nn_astparser_addlocal(prs, iteratortoken) - 1;
    nn_astparser_definevariable(prs, 0);
    /* Create the key local variable. */
    nn_astemit_emitinstruc(prs, NEON_OP_PUSHNULL);
    keyslot = nn_astparser_addlocal(prs, keytoken) - 1;
    nn_astparser_definevariable(prs, keyslot);
    /* create the local value slot */
    nn_astemit_emitinstruc(prs, NEON_OP_PUSHNULL);
    valueslot = nn_astparser_addlocal(prs, valuetoken) - 1;
    nn_astparser_definevariable(prs, 0);
    surroundingloopstart = prs->innermostloopstart;
    surroundingscopedepth = prs->innermostloopscopedepth;
    /*
    // we'll be jumping back to right before the
    // expression after the loop body
    */
    prs->innermostloopstart = nn_astparser_currentblob(prs)->count;
    prs->innermostloopscopedepth = prs->currentfunccompiler->scopedepth;
    /* key = iterable.iter_n__(key) */
    nn_astemit_emitbyteandshort(prs, NEON_OP_LOCALGET, iteratorslot);
    nn_astemit_emitbyteandshort(prs, NEON_OP_LOCALGET, keyslot);
    nn_astemit_emitbyteandshort(prs, NEON_OP_CALLMETHOD, citern);
    nn_astemit_emit1byte(prs, 1);
    nn_astemit_emitbyteandshort(prs, NEON_OP_LOCALSET, keyslot);
    falsejump = nn_astemit_emitjump(prs, NEON_OP_JUMPIFFALSE);
    nn_astemit_emitinstruc(prs, NEON_OP_POPONE);
    /* value = iterable.iter__(key) */
    nn_astemit_emitbyteandshort(prs, NEON_OP_LOCALGET, iteratorslot);
    nn_astemit_emitbyteandshort(prs, NEON_OP_LOCALGET, keyslot);
    nn_astemit_emitbyteandshort(prs, NEON_OP_CALLMETHOD, citer);
    nn_astemit_emit1byte(prs, 1);
    /*
    // Bind the loop value in its own scope. This ensures we get a fresh
    // variable each iteration so that closures for it don't all see the same one.
    */
    nn_astparser_scopebegin(prs);
    /* update the value */
    nn_astemit_emitbyteandshort(prs, NEON_OP_LOCALSET, valueslot);
    nn_astemit_emitinstruc(prs, NEON_OP_POPONE);
    nn_astparser_parsestmt(prs);
    nn_astparser_scopeend(prs);
    nn_astemit_emitloop(prs, prs->innermostloopstart);
    nn_astemit_patchjump(prs, falsejump);
    nn_astemit_emitinstruc(prs, NEON_OP_POPONE);
    nn_astparser_endloop(prs);
    prs->innermostloopstart = surroundingloopstart;
    prs->innermostloopscopedepth = surroundingscopedepth;
    nn_astparser_scopeend(prs);
}

/**
 * switch expression {
 *    case expression {
 *      ...
 *    }
 *    case expression {
 *      ...
 *    }
 *    ...
 * }
 */
static void nn_astparser_parseswitchstmt(NNAstParser* prs)
{
    int i;
    int length;
    int swstate;
    int casecount;
    int switchcode;
    int startoffset;
    int caseends[NEON_CONFIG_ASTMAXSWITCHCASES];
    char* str;
    NNValue jump;
    NNAstTokType casetype;
    NNObjSwitch* sw;
    NNObjString* string;
    /* the expression */
    nn_astparser_consume(prs, NEON_ASTTOK_PARENOPEN, "expected '(' before 'switch'");

    nn_astparser_parseexpression(prs);
    nn_astparser_consume(prs, NEON_ASTTOK_PARENCLOSE, "expected ')' after 'switch'");
    nn_astparser_ignorewhitespace(prs);
    nn_astparser_consume(prs, NEON_ASTTOK_BRACEOPEN, "expected '{' after 'switch' expression");
    nn_astparser_ignorewhitespace(prs);
    /* 0: before all cases, 1: before default, 2: after default */
    swstate = 0;
    casecount = 0;
    sw = nn_object_makeswitch(prs->pstate);
    nn_vm_stackpush(prs->pstate, nn_value_fromobject(sw));
    switchcode = nn_astemit_emitswitch(prs);
    /* nn_astemit_emitbyteandshort(prs, NEON_OP_SWITCH, nn_astparser_pushconst(prs, nn_value_fromobject(sw))); */
    startoffset = nn_astparser_currentblob(prs)->count;
    prs->inswitch = true;
    while(!nn_astparser_match(prs, NEON_ASTTOK_BRACECLOSE) && !nn_astparser_check(prs, NEON_ASTTOK_EOF))
    {
        if(nn_astparser_match(prs, NEON_ASTTOK_KWCASE) || nn_astparser_match(prs, NEON_ASTTOK_KWDEFAULT))
        {
            casetype = prs->prevtoken.type;
            if(swstate == 2)
            {
                nn_astparser_raiseerror(prs, "cannot have another case after a default case");
            }
            if(swstate == 1)
            {
                /* at the end of the previous case, jump over the others... */
                caseends[casecount++] = nn_astemit_emitjump(prs, NEON_OP_JUMPNOW);
            }
            if(casetype == NEON_ASTTOK_KWCASE)
            {
                swstate = 1;
                do
                {
                    nn_astparser_ignorewhitespace(prs);
                    nn_astparser_advance(prs);
                    jump = nn_value_makenumber((double)nn_astparser_currentblob(prs)->count - (double)startoffset);
                    if(prs->prevtoken.type == NEON_ASTTOK_KWTRUE)
                    {
                        nn_valtable_set(&sw->table, nn_value_makebool(true), jump);
                    }
                    else if(prs->prevtoken.type == NEON_ASTTOK_KWFALSE)
                    {
                        nn_valtable_set(&sw->table, nn_value_makebool(false), jump);
                    }
                    else if(prs->prevtoken.type == NEON_ASTTOK_LITERALSTRING || prs->prevtoken.type == NEON_ASTTOK_LITERALRAWSTRING)
                    {
                        str = nn_astparser_compilestring(prs, &length, true);
                        string = nn_string_takelen(prs->pstate, str, length);
                        /* gc fix */
                        nn_vm_stackpush(prs->pstate, nn_value_fromobject(string));
                        nn_valtable_set(&sw->table, nn_value_fromobject(string), jump);
                        /* gc fix */
                        nn_vm_stackpop(prs->pstate);
                    }
                    else if(nn_astparser_checknumber(prs))
                    {
                        nn_valtable_set(&sw->table, nn_astparser_compilenumber(prs), jump);
                    }
                    else
                    {
                        /* pop the switch */
                        nn_vm_stackpop(prs->pstate);
                        nn_astparser_raiseerror(prs, "only constants can be used in 'case' expressions");
                        return;
                    }
                } while(nn_astparser_match(prs, NEON_ASTTOK_COMMA));
                nn_astparser_consume(prs, NEON_ASTTOK_COLON, "expected ':' after 'case' constants");
            }
            else
            {
                nn_astparser_consume(prs, NEON_ASTTOK_COLON, "expected ':' after 'default'");
                swstate = 2;
                sw->defaultjump = nn_astparser_currentblob(prs)->count - startoffset;
            }
        }
        else
        {
            /* otherwise, it's a statement inside the current case */
            if(swstate == 0)
            {
                nn_astparser_raiseerror(prs, "cannot have statements before any case");
            }
            nn_astparser_parsestmt(prs);
        }
    }
    prs->inswitch = false;
    /* if we ended without a default case, patch its condition jump */
    if(swstate == 1)
    {
        caseends[casecount++] = nn_astemit_emitjump(prs, NEON_OP_JUMPNOW);
    }
    /* patch all the case jumps to the end */
    for(i = 0; i < casecount; i++)
    {
        nn_astemit_patchjump(prs, caseends[i]);
    }
    sw->exitjump = nn_astparser_currentblob(prs)->count - startoffset;
    nn_astemit_patchswitch(prs, switchcode, nn_astparser_pushconst(prs, nn_value_fromobject(sw)));
    /* pop the switch */  
    nn_vm_stackpop(prs->pstate);
}

static void nn_astparser_parseifstmt(NNAstParser* prs)
{
    int elsejump;
    int thenjump;
    nn_astparser_parseexpression(prs);
    thenjump = nn_astemit_emitjump(prs, NEON_OP_JUMPIFFALSE);
    nn_astemit_emitinstruc(prs, NEON_OP_POPONE);
    nn_astparser_parsestmt(prs);
    elsejump = nn_astemit_emitjump(prs, NEON_OP_JUMPNOW);
    nn_astemit_patchjump(prs, thenjump);
    nn_astemit_emitinstruc(prs, NEON_OP_POPONE);
    if(nn_astparser_match(prs, NEON_ASTTOK_KWELSE))
    {
        nn_astparser_parsestmt(prs);
    }
    nn_astemit_patchjump(prs, elsejump);
}

static void nn_astparser_parseechostmt(NNAstParser* prs)
{
    nn_astparser_parseexpression(prs);
    nn_astemit_emitinstruc(prs, NEON_OP_ECHO);
    nn_astparser_consumestmtend(prs);
}

static void nn_astparser_parsethrowstmt(NNAstParser* prs)
{
    nn_astparser_parseexpression(prs);
    nn_astemit_emitinstruc(prs, NEON_OP_EXTHROW);
    nn_astparser_discardlocals(prs, prs->currentfunccompiler->scopedepth - 1);
    nn_astparser_consumestmtend(prs);
}

static void nn_astparser_parseassertstmt(NNAstParser* prs)
{
    nn_astparser_consume(prs, NEON_ASTTOK_PARENOPEN, "expected '(' after 'assert'");
    nn_astparser_parseexpression(prs);
    if(nn_astparser_match(prs, NEON_ASTTOK_COMMA))
    {
        nn_astparser_ignorewhitespace(prs);
        nn_astparser_parseexpression(prs);
    }
    else
    {
        nn_astemit_emitinstruc(prs, NEON_OP_PUSHNULL);
    }
    nn_astemit_emitinstruc(prs, NEON_OP_ASSERT);
    nn_astparser_consume(prs, NEON_ASTTOK_PARENCLOSE, "expected ')' after 'assert'");
    nn_astparser_consumestmtend(prs);
}

static void nn_astparser_parsetrystmt(NNAstParser* prs)
{
    int address;
    int type;
    int finally;
    int trybegins;
    int exitjump;
    int continueexecutionaddress;
    bool catchexists;
    bool finalexists;
    #if 0
    if(prs->currentfunccompiler->handlercount == NEON_CONFIG_MAXEXCEPTHANDLERS)
    {
        nn_astparser_raiseerror(prs, "maximum exception handler in scope exceeded");
    }
    #endif
    prs->currentfunccompiler->handlercount++;
    prs->istrying = true;
    nn_astparser_ignorewhitespace(prs);
    trybegins = nn_astemit_emittry(prs);
    /* compile the try body */
    nn_astparser_parsestmt(prs);
    nn_astemit_emitinstruc(prs, NEON_OP_EXPOPTRY);
    exitjump = nn_astemit_emitjump(prs, NEON_OP_JUMPNOW);
    prs->istrying = false;
    /*
    // we can safely use 0 because a program cannot start with a
    // catch or finally block
    */
    address = 0;
    type = -1;
    finally = 0;
    catchexists = false;
    finalexists= false;
    /* catch body must maintain its own scope */
    if(nn_astparser_match(prs, NEON_ASTTOK_KWCATCH))
    {
        catchexists = true;
        nn_astparser_scopebegin(prs);
        nn_astparser_consume(prs, NEON_ASTTOK_PARENOPEN, "expected '(' after 'catch'");
        /*
        nn_astparser_consume(prs, NEON_ASTTOK_IDENTNORMAL, "missing exception class name");
        */
        type = nn_astparser_makeidentconst(prs, &prs->prevtoken);
        address = nn_astparser_currentblob(prs)->count;
        if(nn_astparser_match(prs, NEON_ASTTOK_IDENTNORMAL))
        {
            nn_astparser_createdvar(prs, prs->prevtoken);
        }
        else
        {
            nn_astemit_emitinstruc(prs, NEON_OP_POPONE);
        }
        nn_astparser_consume(prs, NEON_ASTTOK_PARENCLOSE, "expected ')' after 'catch'");
        nn_astemit_emitinstruc(prs, NEON_OP_EXPOPTRY);
        nn_astparser_ignorewhitespace(prs);
        nn_astparser_parsestmt(prs);
        nn_astparser_scopeend(prs);
    }
    else
    {
        type = nn_astparser_pushconst(prs, nn_value_fromobject(nn_string_intern(prs->pstate, "Exception")));
    }
    nn_astemit_patchjump(prs, exitjump);
    if(nn_astparser_match(prs, NEON_ASTTOK_KWFINALLY))
    {
        finalexists = true;
        /*
        // if we arrived here from either the try or handler block,
        // we don't want to continue propagating the exception
        */
        nn_astemit_emitinstruc(prs, NEON_OP_PUSHFALSE);
        finally = nn_astparser_currentblob(prs)->count;
        nn_astparser_ignorewhitespace(prs);
        nn_astparser_parsestmt(prs);
        continueexecutionaddress = nn_astemit_emitjump(prs, NEON_OP_JUMPIFFALSE);
        /* pop the bool off the stack */
        nn_astemit_emitinstruc(prs, NEON_OP_POPONE);
        nn_astemit_emitinstruc(prs, NEON_OP_EXPUBLISHTRY);
        nn_astemit_patchjump(prs, continueexecutionaddress);
        nn_astemit_emitinstruc(prs, NEON_OP_POPONE);
    }
    if(!finalexists && !catchexists)
    {
        nn_astparser_raiseerror(prs, "try block must contain at least one of catch or finally");
    }
    nn_astemit_patchtry(prs, trybegins, type, address, finally);
}

static void nn_astparser_parsereturnstmt(NNAstParser* prs)
{
    prs->isreturning = true;
    /*
    if(prs->currentfunccompiler->type == NEON_FNCONTEXTTYPE_SCRIPT)
    {
        nn_astparser_raiseerror(prs, "cannot return from top-level code");
    }
    */
    if(nn_astparser_match(prs, NEON_ASTTOK_SEMICOLON) || nn_astparser_match(prs, NEON_ASTTOK_NEWLINE))
    {
        nn_astemit_emitreturn(prs);
    }
    else
    {
        if(prs->currentfunccompiler->contexttype == NEON_FNCONTEXTTYPE_INITIALIZER)
        {
            nn_astparser_raiseerror(prs, "cannot return value from constructor");
        }
        if(prs->istrying)
        {
            nn_astemit_emitinstruc(prs, NEON_OP_EXPOPTRY);
        }
        nn_astparser_parseexpression(prs);
        nn_astemit_emitinstruc(prs, NEON_OP_RETURN);
        nn_astparser_consumestmtend(prs);
    }
    prs->isreturning = false;
}

static void nn_astparser_parsewhilestmt(NNAstParser* prs)
{
    int exitjump;
    int surroundingloopstart;
    int surroundingscopedepth;
    surroundingloopstart = prs->innermostloopstart;
    surroundingscopedepth = prs->innermostloopscopedepth;
    /*
    // we'll be jumping back to right before the
    // expression after the loop body
    */
    prs->innermostloopstart = nn_astparser_currentblob(prs)->count;
    prs->innermostloopscopedepth = prs->currentfunccompiler->scopedepth;
    nn_astparser_parseexpression(prs);
    exitjump = nn_astemit_emitjump(prs, NEON_OP_JUMPIFFALSE);
    nn_astemit_emitinstruc(prs, NEON_OP_POPONE);
    nn_astparser_parsestmt(prs);
    nn_astemit_emitloop(prs, prs->innermostloopstart);
    nn_astemit_patchjump(prs, exitjump);
    nn_astemit_emitinstruc(prs, NEON_OP_POPONE);
    nn_astparser_endloop(prs);
    prs->innermostloopstart = surroundingloopstart;
    prs->innermostloopscopedepth = surroundingscopedepth;
}

static void nn_astparser_parsedo_whilestmt(NNAstParser* prs)
{
    int exitjump;
    int surroundingloopstart;
    int surroundingscopedepth;
    surroundingloopstart = prs->innermostloopstart;
    surroundingscopedepth = prs->innermostloopscopedepth;
    /*
    // we'll be jumping back to right before the
    // statements after the loop body
    */
    prs->innermostloopstart = nn_astparser_currentblob(prs)->count;
    prs->innermostloopscopedepth = prs->currentfunccompiler->scopedepth;
    nn_astparser_parsestmt(prs);
    nn_astparser_consume(prs, NEON_ASTTOK_KWWHILE, "expecting 'while' statement");
    nn_astparser_parseexpression(prs);
    exitjump = nn_astemit_emitjump(prs, NEON_OP_JUMPIFFALSE);
    nn_astemit_emitinstruc(prs, NEON_OP_POPONE);
    nn_astemit_emitloop(prs, prs->innermostloopstart);
    nn_astemit_patchjump(prs, exitjump);
    nn_astemit_emitinstruc(prs, NEON_OP_POPONE);
    nn_astparser_endloop(prs);
    prs->innermostloopstart = surroundingloopstart;
    prs->innermostloopscopedepth = surroundingscopedepth;
}

static void nn_astparser_parsecontinuestmt(NNAstParser* prs)
{
    if(prs->innermostloopstart == -1)
    {
        nn_astparser_raiseerror(prs, "'continue' can only be used in a loop");
    }
    /*
    // discard local variables created in the loop
    //  discard_local(prs, prs->innermostloopscopedepth);
    */
    nn_astparser_discardlocals(prs, prs->innermostloopscopedepth + 1);
    /* go back to the top of the loop */
    nn_astemit_emitloop(prs, prs->innermostloopstart);
    nn_astparser_consumestmtend(prs);
}

static void nn_astparser_parsebreakstmt(NNAstParser* prs)
{
    if(!prs->inswitch)
    {
        if(prs->innermostloopstart == -1)
        {
            nn_astparser_raiseerror(prs, "'break' can only be used in a loop");
        }
        /* discard local variables created in the loop */
        #if 0
        int i;
        for(i = prs->currentfunccompiler->localcount - 1; i >= 0 && prs->currentfunccompiler->locals[i].depth >= prs->currentfunccompiler->scopedepth; i--)
        {
            if (prs->currentfunccompiler->locals[i].iscaptured)
            {
                nn_astemit_emitinstruc(prs, NEON_OP_UPVALUECLOSE);
            }
            else
            {
                nn_astemit_emitinstruc(prs, NEON_OP_POPONE);
            }
        }
        #endif
        nn_astparser_discardlocals(prs, prs->innermostloopscopedepth + 1);
        nn_astemit_emitjump(prs, NEON_OP_BREAK_PL);
    }
    nn_astparser_consumestmtend(prs);
}

static void nn_astparser_synchronize(NNAstParser* prs)
{
    prs->panicmode = false;
    while(prs->currtoken.type != NEON_ASTTOK_EOF)
    {
        if(prs->currtoken.type == NEON_ASTTOK_NEWLINE || prs->currtoken.type == NEON_ASTTOK_SEMICOLON)
        {
            return;
        }
        switch(prs->currtoken.type)
        {
            case NEON_ASTTOK_KWCLASS:
            case NEON_ASTTOK_KWFUNCTION:
            case NEON_ASTTOK_KWVAR:
            case NEON_ASTTOK_KWFOREACH:
            case NEON_ASTTOK_KWIF:
            case NEON_ASTTOK_KWEXTENDS:
            case NEON_ASTTOK_KWSWITCH:
            case NEON_ASTTOK_KWCASE:
            case NEON_ASTTOK_KWFOR:
            case NEON_ASTTOK_KWDO:
            case NEON_ASTTOK_KWWHILE:
            case NEON_ASTTOK_KWECHO:
            case NEON_ASTTOK_KWASSERT:
            case NEON_ASTTOK_KWTRY:
            case NEON_ASTTOK_KWCATCH:
            case NEON_ASTTOK_KWTHROW:
            case NEON_ASTTOK_KWRETURN:
            case NEON_ASTTOK_KWSTATIC:
            case NEON_ASTTOK_KWTHIS:
            case NEON_ASTTOK_KWSUPER:
            case NEON_ASTTOK_KWFINALLY:
            case NEON_ASTTOK_KWIN:
            case NEON_ASTTOK_KWIMPORT:
            case NEON_ASTTOK_KWAS:
                return;
            default:
                /* do nothing */
            ;
        }
        nn_astparser_advance(prs);
    }
}

/*
* $keeplast: whether to emit code that retains or discards the value of the last statement/expression.
* SHOULD NOT BE USED FOR ORDINARY SCRIPTS as it will almost definitely result in the stack containing invalid values.
*/
NNObjFunction* nn_astparser_compilesource(NNState* state, NNObjModule* module, const char* source, NNBlob* blob, bool fromimport, bool keeplast)
{
    NNAstFuncCompiler fnc;
    NNAstLexer* lexer;
    NNAstParser* parser;
    NNObjFunction* function;
    (void)blob;
    lexer = nn_astlex_make(state, source);
    parser = nn_astparser_makeparser(state, lexer, module, keeplast);
    nn_astfunccompiler_init(parser, &fnc, NEON_FNCONTEXTTYPE_SCRIPT, true);
    fnc.fromimport = fromimport;
    nn_astparser_runparser(parser);
    function = nn_astparser_endcompiler(parser, true);
    if(parser->haderror)
    {
        function = NULL;
    }
    nn_astlex_destroy(state, lexer);
    nn_astparser_destroy(parser);
    return function;
}


