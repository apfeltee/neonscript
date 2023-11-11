

bool neon_lexutil_isdigit(char c)
{
    return c >= '0' && c <= '9';
}

bool neon_lexutil_isbinary(char c)
{
    return c == '0' || c == '1';
}

bool neon_lexutil_isalpha(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

bool neon_lexutil_isoctal(char c)
{
    return c >= '0' && c <= '7';
}

bool neon_lexutil_ishexadecimal(char c)
{
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

NeonAstScanner* neon_astlex_make(NeonState* state, const char* source, size_t len)
{
    NeonAstScanner* scn;
    scn = (NeonAstScanner*)malloc(sizeof(NeonAstScanner));
    scn->pvm = state;
    scn->start = source;
    scn->current = source;
    scn->line = 1;
    scn->maxlength = len;
    return scn;
}

void neon_astlex_destroy(NeonAstScanner* scn)
{
    free(scn);
}


bool neon_astlex_isatend(NeonAstScanner* scn)
{
    return *scn->current == '\0';
}

char neon_astlex_advance(NeonAstScanner* scn)
{
    scn->current++;
    return scn->current[-1];
}

char neon_astlex_peekcurrent(NeonAstScanner* scn)
{
    return *scn->current;
}

char neon_astlex_peekprevious(NeonAstScanner* scn)
{
    return scn->current[-1];
}


char neon_astlex_peeknext(NeonAstScanner* scn)
{
    if(neon_astlex_isatend(scn))
    {
        return '\0';
    }
    return scn->current[1];
}

bool neon_astlex_match(NeonAstScanner* scn, char expected)
{
    if(neon_astlex_isatend(scn))
        return false;
    if(*scn->current != expected)
        return false;
    scn->current++;
    return true;
}

NeonAstToken neon_astlex_maketoken(NeonAstScanner* scn, NeonAstTokType type)
{
    NeonAstToken token;
    token.type = type;
    token.start = scn->start;
    token.length = (int)(scn->current - scn->start);
    token.line = scn->line;
    return token;
}

NeonAstToken neon_astlex_makeerrortoken(NeonAstScanner* scn, const char* message)
{
    NeonAstToken token;
    token.type = NEON_TOK_ERROR;
    token.start = message;
    token.length = (int)strlen(message);
    token.line = scn->line;
    return token;
}

void neon_astlex_skipspace(NeonAstScanner* scn)
{
    char c;
    for(;;)
    {
        c = neon_astlex_peekcurrent(scn);
        switch(c)
        {
            case ' ':
            case '\r':
            case '\t':
                {
                    neon_astlex_advance(scn);
                }
                break;
            case '\n':
                {
                    scn->line++;
                    neon_astlex_advance(scn);
                }
                break;
            case '/':
                {
                    if(neon_astlex_peeknext(scn) == '/')
                    {
                        // A comment goes until the end of the line.
                        while(neon_astlex_peekcurrent(scn) != '\n' && !neon_astlex_isatend(scn))
                            neon_astlex_advance(scn);
                    }
                    else
                    {
                        return;
                    }
                }
                break;
            default:
                return;
        }
    }
}

NeonAstTokType neon_astlex_scankeyword(NeonAstScanner* scn)
{
    static const struct
    {
        const char* str;
        NeonAstTokType type;
    } keywords[] =
    {
        {"and", NEON_TOK_KWAND},
        {"break", NEON_TOK_KWBREAK},
        {"continue", NEON_TOK_KWCONTINUE},
        {"class", NEON_TOK_KWCLASS},
        {"else", NEON_TOK_KWELSE},
        {"false", NEON_TOK_KWFALSE},
        {"for", NEON_TOK_KWFOR},
        {"function", NEON_TOK_KWFUNCTION},
        {"fun", NEON_TOK_KWFUNCTION},
        {"if", NEON_TOK_KWIF},
        {"nil", NEON_TOK_KWNIL},
        {"null", NEON_TOK_KWNIL},
        {"new", NEON_TOK_KWNEW},
        {"or", NEON_TOK_KWOR},
        {"debugprint", NEON_TOK_KWDEBUGPRINT},
        {"typeof", NEON_TOK_KWTYPEOF},
        {"global", NEON_TOK_KWGLOBAL},
        {"return", NEON_TOK_KWRETURN},
        {"super", NEON_TOK_KWSUPER},
        {"this", NEON_TOK_KWTHIS},
        {"true", NEON_TOK_KWTRUE},
        {"var", NEON_TOK_KWVAR},
        {"while", NEON_TOK_KWWHILE},
        {NULL, (NeonAstTokType)0},
    };
    size_t i;
    size_t kwlen;
    size_t ofs;
    const char* kwtext;
    for(i=0; keywords[i].str != NULL; i++)
    {
        kwtext = keywords[i].str;
        kwlen = strlen(kwtext);
        ofs = (scn->current - scn->start);
        if((ofs == (0 + kwlen)) && (memcmp(scn->start + 0, kwtext, kwlen) == 0))
        {
            return keywords[i].type;
        }
    }
    return NEON_TOK_IDENTIFIER;
}

NeonAstToken neon_astlex_scanident(NeonAstScanner* scn)
{
    while(neon_lexutil_isalpha(neon_astlex_peekcurrent(scn)) || neon_lexutil_isdigit(neon_astlex_peekcurrent(scn)))
    {
        neon_astlex_advance(scn);
    }
    return neon_astlex_maketoken(scn, neon_astlex_scankeyword(scn));
}

/*
NeonAstToken neon_astlex_scannumber(NeonAstScanner* scn)
{
    while(neon_astlex_isdigit(neon_astlex_peekcurrent(scn)))
    {
        neon_astlex_advance(scn);
    }
    // Look for a fractional part.
    if(neon_astlex_peekcurrent(scn) == '.' && neon_astlex_isdigit(neon_astlex_peeknext(scn)))
    {
        // Consume the ".".
        neon_astlex_advance(scn);
        while(neon_astlex_isdigit(neon_astlex_peekcurrent(scn)))
        {
            neon_astlex_advance(scn);
        }
    }
    return neon_astlex_maketoken(scn, NEON_TOK_NUMBER);
}
*/

NeonAstToken neon_astlex_scannumber(NeonAstScanner* scn)
{
    // handle binary, octal and hexadecimals
    if(neon_astlex_peekprevious(scn) == '0')
    {
        if(neon_astlex_match(scn, 'b'))
        {// binary number
            while(neon_lexutil_isbinary(neon_astlex_peekcurrent(scn)))
            {
                neon_astlex_advance(scn);
            }
            return neon_astlex_maketoken(scn, NEON_TOK_BINNUMBER);
        }
        else if(neon_astlex_match(scn, 'c'))
        {
            while(neon_lexutil_isoctal(neon_astlex_peekcurrent(scn)))
            {
                neon_astlex_advance(scn);
            }
            return neon_astlex_maketoken(scn, NEON_TOK_OCTNUMBER);
        }
        else if(neon_astlex_match(scn, 'x'))
        {
            while(neon_lexutil_ishexadecimal(neon_astlex_peekcurrent(scn)))
            {
                neon_astlex_advance(scn);
            }
            return neon_astlex_maketoken(scn, NEON_TOK_HEXNUMBER);
        }
    }
    while(neon_lexutil_isdigit(neon_astlex_peekcurrent(scn)))
    {
        neon_astlex_advance(scn);
    }
    // dots(.) are only valid here when followed by a digit
    if(neon_astlex_peekcurrent(scn) == '.' && neon_lexutil_isdigit(neon_astlex_peeknext(scn)))
    {
        neon_astlex_advance(scn);
        while(neon_lexutil_isdigit(neon_astlex_peekcurrent(scn)))
        {
            neon_astlex_advance(scn);
        }
        // E or e are only valid here when followed by a digit and occurring after a
        // dot
        if((neon_astlex_peekcurrent(scn) == 'e' || neon_astlex_peekcurrent(scn) == 'E') && (neon_astlex_peeknext(scn) == '+' || neon_astlex_peeknext(scn) == '-'))
        {
            neon_astlex_advance(scn);
            neon_astlex_advance(scn);
            while(neon_lexutil_isdigit(neon_astlex_peekcurrent(scn)))
            {
                neon_astlex_advance(scn);
            }
        }
    }
    return neon_astlex_maketoken(scn, NEON_TOK_NUMBER);
}


NeonAstToken neon_astlex_scanstring(NeonAstScanner* scn, char quote)
{
    while(neon_astlex_peekcurrent(scn) != quote && !neon_astlex_isatend(scn))
    {
        if(neon_astlex_peekcurrent(scn) == '\\' && (neon_astlex_peeknext(scn) == quote || neon_astlex_peeknext(scn) == '\\'))
        {
            neon_astlex_advance(scn);
        }
        neon_astlex_advance(scn);
    }
    if(neon_astlex_isatend(scn))
    {
        return neon_astlex_makeerrortoken(scn, "unterminated string");
    }
    neon_astlex_match(scn, quote);// the closing quote
    return neon_astlex_maketoken(scn, NEON_TOK_STRING);
}

NeonAstToken neon_astlex_scantoken(NeonAstScanner* scn)
{
    char c;
    neon_astlex_skipspace(scn);
    scn->start = scn->current;
    if(neon_astlex_isatend(scn))
    {
        return neon_astlex_maketoken(scn, NEON_TOK_EOF);
    }
    c = neon_astlex_advance(scn);
    if(neon_lexutil_isalpha(c))
    {
        return neon_astlex_scanident(scn);
    }
    if(neon_lexutil_isdigit(c))
    {
        return neon_astlex_scannumber(scn);
    }
    switch(c)
    {
        case '\n':
            {
                return neon_astlex_maketoken(scn, NEON_TOK_NEWLINE);
            }
            break;
        case '(':
            {
                return neon_astlex_maketoken(scn, NEON_TOK_PARENOPEN);
            }
            break;
        case ')':
            {
                return neon_astlex_maketoken(scn, NEON_TOK_PARENCLOSE);
            }
            break;
        case '{':
            {
                return neon_astlex_maketoken(scn, NEON_TOK_BRACEOPEN);
            }
            break;
        case '}':
            {
                return neon_astlex_maketoken(scn, NEON_TOK_BRACECLOSE);
            }
            break;
        case '[':
            {
                return neon_astlex_maketoken(scn, NEON_TOK_BRACKETOPEN);
            }
            break;
        case ']':
            {
                return neon_astlex_maketoken(scn, NEON_TOK_BRACKETCLOSE);
            }
            break;
        case ';':
            {
                return neon_astlex_maketoken(scn, NEON_TOK_SEMICOLON);
            }
            break;
        case ',':
            {
                return neon_astlex_maketoken(scn, NEON_TOK_COMMA);
            }
            break;
        case '.':
            {
                return neon_astlex_maketoken(scn, NEON_TOK_DOT);
            }
            break;
        case '-':
            {
                if(neon_astlex_match(scn, '-'))
                {
                    return neon_astlex_maketoken(scn, NEON_TOK_DECREMENT);
                }
                else if(neon_astlex_match(scn, '='))
                {
                    return neon_astlex_maketoken(scn, NEON_TOK_ASSIGNMINUS);
                }
                return neon_astlex_maketoken(scn, NEON_TOK_MINUS);
            }
            break;
        case '+':
            {
                if(neon_astlex_match(scn, '+'))
                {
                    return neon_astlex_maketoken(scn, NEON_TOK_INCREMENT);
                }
                else if(neon_astlex_match(scn, '='))
                {
                    return neon_astlex_maketoken(scn, NEON_TOK_ASSIGNPLUS);
                }
                return neon_astlex_maketoken(scn, NEON_TOK_PLUS);
            }
            break;
        case '&':
            {
                if(neon_astlex_match(scn, '&'))
                {
                    return neon_astlex_maketoken(scn, NEON_TOK_KWAND);
                }
                return neon_astlex_maketoken(scn, NEON_TOK_BINAND);
            }
            break;
        case '|':
            {
                if(neon_astlex_match(scn, '|'))
                {
                    return neon_astlex_maketoken(scn, NEON_TOK_KWOR);
                }
                return neon_astlex_maketoken(scn, NEON_TOK_BINOR);
            }
            break;
        case '^':
            {
                return neon_astlex_maketoken(scn, NEON_TOK_BINXOR);
            }
            break;
        case '%':
            {
                if(neon_astlex_match(scn, '='))
                {
                    return neon_astlex_maketoken(scn, NEON_TOK_ASSIGNMODULO);
                }
                return neon_astlex_maketoken(scn, NEON_TOK_MODULO);
            }
            break;
        case '/':
            {
                if(neon_astlex_match(scn, '='))
                {
                    return neon_astlex_maketoken(scn, NEON_TOK_ASSIGNDIV);
                }
                return neon_astlex_maketoken(scn, NEON_TOK_DIVIDE);
            }
            break;
        case '*':
            {
                if(neon_astlex_match(scn, '='))
                {
                    return neon_astlex_maketoken(scn, NEON_TOK_ASSIGNMULT);
                }
                return neon_astlex_maketoken(scn, NEON_TOK_MULTIPLY);
            }
            break;
        case '~':
            {
                return neon_astlex_maketoken(scn, NEON_TOK_TILDE);
            }
            break;
        case '!':
            {
                if(neon_astlex_match(scn, '='))
                {
                    return neon_astlex_maketoken(scn, NEON_TOK_COMPNOTEQUAL);
                }
                return neon_astlex_maketoken(scn, NEON_TOK_EXCLAM);
            }
            break;
        case '=':
            {
                if(neon_astlex_match(scn, '='))
                {
                    return neon_astlex_maketoken(scn, NEON_TOK_COMPEQUAL);
                }
                return neon_astlex_maketoken(scn, NEON_TOK_ASSIGN);
            }
            break;
        case '<':
            {
                if(neon_astlex_match(scn, '='))
                {
                    return neon_astlex_maketoken(scn, NEON_TOK_COMPLESSEQUAL);
                }
                else if(neon_astlex_match(scn, '<'))
                {
                    return neon_astlex_maketoken(scn, NEON_TOK_SHIFTLEFT);
                }
                return neon_astlex_maketoken(scn, NEON_TOK_COMPLESSTHAN);
            }
            break;
        case '>':
            {
                if(neon_astlex_match(scn, '='))
                {
                    return neon_astlex_maketoken(scn, NEON_TOK_COMPGREATEREQUAL);
                }
                else if(neon_astlex_match(scn, '>'))
                {
                    return neon_astlex_maketoken(scn, NEON_TOK_SHIFTRIGHT);
                }
                return neon_astlex_maketoken(scn, NEON_TOK_COMPGREATERTHAN);
            }
            break;
        case '"':
            {
                return neon_astlex_scanstring(scn, '"');
            }
            break;
    }
    return neon_astlex_makeerrortoken(scn, "unexpected character");
}

NeonAstParser* neon_astparser_make(NeonState* state)
{
    NeonAstParser* prs;
    prs = (NeonAstParser*)malloc(sizeof(NeonAstParser));
    prs->pvm = state;
    prs->currcompiler = NULL;
    prs->currclass = NULL;
    prs->haderror = false;
    prs->panicmode = false;
    prs->innermostloopstart = -1;
    prs->innermostloopscopedepth = 0;
    prs->blockcount = 0;
    return prs;
}

void neon_astparser_destroy(NeonAstParser* prs)
{
    if(prs->pscn != NULL)
    {
        neon_astlex_destroy(prs->pscn);
    }
    free(prs);
}

NeonBinaryBlob* neon_astparser_currentblob(NeonAstParser* prs)
{
    return prs->currcompiler->currfunc->blob;
}

void neon_astparser_vraiseattoken(NeonAstParser* prs, NeonAstToken* token, const char* message, va_list va)
{
    if(prs->panicmode)
    {
        return;
    }
    prs->panicmode = true;
    fprintf(stderr, "[line %d] error", token->line);
    if(token->type == NEON_TOK_EOF)
    {
        fprintf(stderr, " at end");
    }
    else if(token->type == NEON_TOK_ERROR)
    {
        // Nothing.
    }
    else
    {
        fprintf(stderr, " at '%.*s'", token->length, token->start);
    }
    fprintf(stderr, ": ");
    vfprintf(stderr, message, va);
    fprintf(stderr, "\n");
    prs->haderror = true;
}

void neon_astparser_raiseerror(NeonAstParser* prs, const char* message, ...)
{
    va_list va;
    va_start(va, message);
    neon_astparser_vraiseattoken(prs, &prs->previous, message, va);
    va_end(va);
}

void neon_astparser_raiseatcurrent(NeonAstParser* prs, const char* message, ...)
{
    va_list va;
    va_start(va, message);
    neon_astparser_vraiseattoken(prs, &prs->current, message, va);
    va_end(va);
}

void neon_astparser_skipsemicolon(NeonAstParser* prs)
{
    
    while(true)
    {
        if(!neon_astparser_match(prs, NEON_TOK_NEWLINE))
        {
            break;
        }
    }
    while(true)
    {
        if(!neon_astparser_match(prs, NEON_TOK_SEMICOLON))
        {
            break;
        }
    }
}

void neon_astparser_advance(NeonAstParser* prs)
{
    prs->previous = prs->current;
    for(;;)
    {
        prs->current = neon_astlex_scantoken(prs->pscn);
        if(prs->current.type != NEON_TOK_ERROR)
        {
            break;
        }
        neon_astparser_raiseatcurrent(prs, prs->current.start);
    }
}

void neon_astparser_consume(NeonAstParser* prs, NeonAstTokType type, const char* message)
{
    if(prs->current.type == type)
    {
        neon_astparser_advance(prs);
        return;
    }
    neon_astparser_raiseatcurrent(prs, message);
}

bool neon_astparser_checkcurrent(NeonAstParser* prs, NeonAstTokType type)
{
    return prs->current.type == type;
}

bool neon_astparser_match(NeonAstParser* prs, NeonAstTokType type)
{
    if(!neon_astparser_checkcurrent(prs, type))
    {
        return false;
    }
    neon_astparser_advance(prs);
    return true;
}

void neon_astparser_emit1byte(NeonAstParser* prs, int32_t byte)
{
    neon_blob_pushbyte(prs->pvm, neon_astparser_currentblob(prs), byte, prs->previous.line);
}

void neon_astparser_emit2byte(NeonAstParser* prs, int32_t byte1, int32_t byte2)
{
    neon_astparser_emit1byte(prs, byte1);
    neon_astparser_emit1byte(prs, byte2);
}

void neon_astparser_emitloop(NeonAstParser* prs, int loopstart)
{
    int offset;
    neon_astparser_emit1byte(prs, NEON_OP_LOOP);
    offset = neon_astparser_currentblob(prs)->count - loopstart + 2;
    if(offset > UINT16_MAX)
    {
        neon_astparser_raiseerror(prs, "loop body too large");
    }
    neon_astparser_emit1byte(prs, (offset >> 8) & 0xff);
    neon_astparser_emit1byte(prs, offset & 0xff);
}

#include "codeargs.h"

int neon_astparser_getcodeargscount(const int32_t* bytecode, const NeonValArray* constants, int ip)
{
    int rc;
    //const char* os;
    rc = neon_astparser_realgetcodeargscount(bytecode, constants, ip);
    //os = neon_dbg_op2str(bytecode[ip]);
    //fprintf(stderr, "getcodeargscount(..., code=%s) = %d\n", os, rc);
    return rc;
}

/*
void neon_astparser_startloop(NeonAstParser* prs, NeonAstLoop* loop)
{
    loop->enclosing = prs->currcompiler->loop;
    loop->start = neon_astparser_currentblob(prs)->count;
    loop->scopedepth = prs->currcompiler->scopedepth;
    prs->currcompiler->loop = loop;
}

void neon_astparser_endloop(NeonAstParser* p)
{
    int i;
    int bclen;
    bclen = p->currcompiler->currfunc->blob->count;
    i = p->innermostloopstart;
    while(i < bclen)
    {
        if(p->currcompiler->currfunc->blob->bincode[i] == NEON_OP_PSEUDOBREAK)
        {
            p->currcompiler->currfunc->blob->bincode[i] = NEON_OP_JUMPNOW;
            neon_astparser_emitpatchjump(p, i + 1);
        }
        else
        {
            i += 1 + neon_astparser_getcodeargscount(p->currcompiler->currfunc->blob->bincode, p->currcompiler->currfunc->blob->constants, i);
        }
    }
}
*/

int neon_astparser_emitjump(NeonAstParser* prs, int32_t instruction)
{
    neon_astparser_emit1byte(prs, instruction);
    neon_astparser_emit1byte(prs, 0xff);
    neon_astparser_emit1byte(prs, 0xff);
    return neon_astparser_currentblob(prs)->count - 2;
}

void neon_astparser_emitreturn(NeonAstParser* prs, bool fromtoplevel)
{
    if(prs->currcompiler->type == NEON_TYPE_INITIALIZER)
    {
        neon_astparser_emit2byte(prs, NEON_OP_LOCALGET, 0);
    }
    else
    {
        //neon_astparser_emit1byte(prs, NEON_OP_PUSHNIL);
    }
    if(fromtoplevel && prs->iseval)
    {
        neon_astparser_emit1byte(prs, NEON_OP_RESTOREFRAME);
    }
    neon_astparser_emit1byte(prs, NEON_OP_RETURN);
}

int32_t neon_astparser_makeconstant(NeonAstParser* prs, NeonValue value)
{
    int constant;
    constant = neon_blob_pushconst(prs->pvm, neon_astparser_currentblob(prs), value);
    if(constant > UINT8_MAX)
    {
        neon_astparser_raiseerror(prs, "too many constants in one blob");
        return 0;
    }
    return (int32_t)constant;
}

void neon_astparser_emitconstant(NeonAstParser* prs, NeonValue value)
{
    neon_astparser_emit2byte(prs, NEON_OP_PUSHCONST, neon_astparser_makeconstant(prs, value));
}

void neon_astparser_emitpatchjump(NeonAstParser* prs, int offset)
{
    int jump;
    // -2 to adjust for the bytecode for the jump offset itself.
    jump = neon_astparser_currentblob(prs)->count - offset - 2;
    if(jump > UINT16_MAX)
    {
        neon_astparser_raiseerror(prs, "too much code to jump over");
    }
    neon_astparser_currentblob(prs)->bincode[offset] = (jump >> 8) & 0xff;
    neon_astparser_currentblob(prs)->bincode[offset + 1] = jump & 0xff;
}

void neon_astparser_compilerinit(NeonAstParser* prs, NeonAstCompiler* compiler, NeonAstFuncType type, const char* name)
{
    size_t nlen;
    const char* nstr;
    NeonAstLocal* local;
    compiler->enclosing = prs->currcompiler;
    compiler->currfunc = NULL;
    compiler->type = type;
    compiler->localcount = 0;
    compiler->scopedepth = 0;
    compiler->currfunc = neon_object_makefunction(prs->pvm);
    prs->currcompiler = compiler;
    if(type != NEON_TYPE_SCRIPT)
    {
        if(name != NULL)
        {
            nstr = name;
            nlen = strlen(name);
        }
        else
        {
            nstr = prs->previous.start;
            nlen = prs->previous.length;
        }
        prs->currcompiler->currfunc->name = neon_string_copy(prs->pvm, nstr, nlen);
    }
    local = &prs->currcompiler->locals[prs->currcompiler->localcount++];
    local->depth = 0;
    local->iscaptured = false;
    /* Calls and Functions init-function-slot < Methods and Initializers slot-zero */
    /*
    local->name.start = "";
    local->name.length = 0;
    */
    if(type != NEON_TYPE_FUNCTION)
    {
        local->name.start = "this";
        local->name.length = 4;
    }
    else
    {
        local->name.start = "";
        local->name.length = 0;
    }
}

NeonObjScriptFunction* neon_astparser_compilerfinish(NeonAstParser* prs, bool ismainfn)
{
    NeonObjScriptFunction* fn;
    neon_astparser_emitreturn(prs, true);
    if(ismainfn)
    {
        //neon_astparser_emit1byte(prs, NEON_OP_HALTVM);
    }
    fn = prs->currcompiler->currfunc;
#if (DEBUG_PRINT_CODE == 1)
    if(!prs->haderror)
    {
        neon_blob_disasm(prs->pvm, prs->pvm->stderrwriter, neon_astparser_currentblob(prs), fn->name != NULL ? fn->name->sbuf->data : "<script>");
    }
#endif
    prs->currcompiler = prs->currcompiler->enclosing;
    return fn;
}

void neon_astparser_scopebegin(NeonAstParser* prs)
{
    prs->currcompiler->scopedepth++;
}

void neon_astparser_scopeend(NeonAstParser* prs)
{
    NeonAstCompiler* pc;
    pc = prs->currcompiler;
    pc->scopedepth--;
    while(pc->localcount > 0 && pc->locals[pc->localcount - 1].depth > pc->scopedepth)
    {
        if(pc->locals[pc->localcount - 1].iscaptured)
        {
            neon_astparser_emit1byte(prs, NEON_OP_UPVALCLOSE);
        }
        else
        {
            neon_astparser_emit1byte(prs, NEON_OP_POPONE);
        }
        pc->localcount--;
    }
}

int32_t neon_astparser_makeidentconstant(NeonAstParser* prs, NeonAstToken* name)
{
    return neon_astparser_makeconstant(prs, neon_value_fromobject(neon_string_copy(prs->pvm, name->start, name->length)));
}

bool neon_astparser_identsequal(NeonAstToken* a, NeonAstToken* b)
{
    if(a->length != b->length)
    {
        return false;
    }
    return memcmp(a->start, b->start, a->length) == 0;
}


void neon_astparser_addlocal(NeonAstParser* prs, NeonAstToken name)
{
    NeonAstLocal* local;
    if(prs->currcompiler->localcount == NEON_MAX_COMPLOCALS)
    {
        neon_astparser_raiseerror(prs, "too many local variables in function");
        return;
    }
    local = &prs->currcompiler->locals[prs->currcompiler->localcount++];
    local->name = name;
    local->depth = -1;
    local->iscaptured = false;
}

void neon_astparser_discardlocals(NeonAstParser* p, int depth)
{
    int i;
    if(p->currcompiler->scopedepth == -1)
    {
        neon_astparser_raiseerror(p, "cannot exit top-level scope");
    }
    for(i = p->currcompiler->localcount - 1; i >= 0 && p->currcompiler->locals[i].depth > depth; i--)
    {
        if(p->currcompiler->locals[i].iscaptured)
        {
            neon_astparser_emit1byte(p, NEON_OP_UPVALCLOSE);
        }
        else
        {
            neon_astparser_emit1byte(p, NEON_OP_POPONE);
        }
    }
}

void neon_astparser_parsevarident(NeonAstParser* prs)
{
    int i;
    NeonAstLocal* local;
    NeonAstToken* name;
    if(prs->currcompiler->scopedepth == 0)
    {
        return;
    }
    name = &prs->previous;
    for(i = prs->currcompiler->localcount - 1; i >= 0; i--)
    {
        local = &prs->currcompiler->locals[i];
        if(local->depth != -1 && local->depth < prs->currcompiler->scopedepth)
        {
            break;// [negative]
        }
        if(neon_astparser_identsequal(name, &local->name))
        {
            neon_astparser_raiseerror(prs, "already a variable with this name in this scope");
        }
    }
    neon_astparser_addlocal(prs, *name);
}

void neon_astparser_markinit(NeonAstParser* prs)
{
    if(prs->currcompiler->scopedepth == 0)
    {
        return;
    }
    prs->currcompiler->locals[prs->currcompiler->localcount - 1].depth = prs->currcompiler->scopedepth;
}

void neon_astparser_emitdefvar(NeonAstParser* prs, int32_t global)
{
    if(prs->currcompiler->scopedepth > 0)
    {
        neon_astparser_markinit(prs);
        return;
    }
    neon_astparser_emit2byte(prs, NEON_OP_GLOBALDEFINE, global);
}

int32_t neon_astparser_parsearglist(NeonAstParser* prs)
{
    int32_t argc;
    argc = 0;
    if(!neon_astparser_checkcurrent(prs, NEON_TOK_PARENCLOSE))
    {
        do
        {
            neon_astparser_parseexpr(prs);
            if(argc == 255)
            {
                neon_astparser_raiseerror(prs, "cannot have more than 255 arguments");
            }
            argc++;
        } while(neon_astparser_match(prs, NEON_TOK_COMMA));
    }
    neon_astparser_consume(prs, NEON_TOK_PARENCLOSE, "expected ')' after arguments");
    return argc;
}

void neon_astparser_ruleand(NeonAstParser* prs, NeonAstToken previous, bool canassign)
{
    int endjump;
    (void)previous;
    (void)canassign;
    endjump = neon_astparser_emitjump(prs, NEON_OP_JUMPIFFALSE);
    neon_astparser_emit1byte(prs, NEON_OP_POPONE);
    neon_astparser_parseprec(prs, NEON_PREC_AND);
    neon_astparser_emitpatchjump(prs, endjump);
}

void neon_astparser_rulebinary(NeonAstParser* prs, NeonAstToken previous, bool canassign)
{
    NeonAstRule* rule;
    NeonAstTokType ot;
    (void)previous;
    (void)canassign;
    ot = prs->previous.type;
    rule = neon_astparser_getrule(ot);
    neon_astparser_parseprec(prs, (NeonAstPrecedence)(rule->precedence + 1));
    switch(ot)
    {
        case NEON_TOK_COMPNOTEQUAL:
            neon_astparser_emit2byte(prs, NEON_OP_EQUAL, NEON_OP_PRIMNOT);
            break;
        case NEON_TOK_COMPEQUAL:
            neon_astparser_emit1byte(prs, NEON_OP_EQUAL);
            break;
        case NEON_TOK_COMPGREATERTHAN:
            neon_astparser_emit1byte(prs, NEON_OP_PRIMGREATER);
            break;
        case NEON_TOK_COMPGREATEREQUAL:
            neon_astparser_emit2byte(prs, NEON_OP_PRIMLESS, NEON_OP_PRIMNOT);
            break;
        case NEON_TOK_COMPLESSTHAN:
            neon_astparser_emit1byte(prs, NEON_OP_PRIMLESS);
            break;
        case NEON_TOK_COMPLESSEQUAL:
            neon_astparser_emit2byte(prs, NEON_OP_PRIMGREATER, NEON_OP_PRIMNOT);
            break;
        case NEON_TOK_PLUS:
            neon_astparser_emit1byte(prs, NEON_OP_PRIMADD);
            break;
        case NEON_TOK_MINUS:
            neon_astparser_emit1byte(prs, NEON_OP_PRIMSUBTRACT);
            break;
        case NEON_TOK_MULTIPLY:
            neon_astparser_emit1byte(prs, NEON_OP_PRIMMULTIPLY);
            break;
        case NEON_TOK_DIVIDE:
            neon_astparser_emit1byte(prs, NEON_OP_PRIMDIVIDE);
            break;
        case NEON_TOK_MODULO:
            neon_astparser_emit1byte(prs, NEON_OP_PRIMMODULO);
            break;
        case NEON_TOK_BINAND:
            neon_astparser_emit1byte(prs, NEON_OP_PRIMBINAND);
            break;
        case NEON_TOK_BINOR:
            neon_astparser_emit1byte(prs, NEON_OP_PRIMBINOR);
            break;
        case NEON_TOK_BINXOR:
            neon_astparser_emit1byte(prs, NEON_OP_PRIMBINXOR);
            break;
        case NEON_TOK_SHIFTLEFT:
            neon_astparser_emit1byte(prs, NEON_OP_PRIMSHIFTLEFT);
            break;
        case NEON_TOK_SHIFTRIGHT:
            neon_astparser_emit1byte(prs, NEON_OP_PRIMSHIFTRIGHT);
            break;
        default:
            return;// Unreachable.
    }
}

void neon_astparser_rulecall(NeonAstParser* prs, NeonAstToken previous, bool canassign)
{
    int32_t argc;
    (void)previous;
    (void)canassign;
    argc = neon_astparser_parsearglist(prs);
    neon_astparser_emit2byte(prs, NEON_OP_CALLCALLABLE, argc);
}

void neon_astparser_ruledot(NeonAstParser* prs, NeonAstToken previous, bool canassign)
{
    int32_t getop;
    int32_t setop;
    int32_t name;
    int32_t argc;
    (void)previous;
    (void)canassign;
    neon_astparser_consume(prs, NEON_TOK_IDENTIFIER, "expect property name after '.'.");
    name = neon_astparser_makeidentconstant(prs, &prs->previous);
    if(canassign && neon_astparser_match(prs, NEON_TOK_ASSIGN))
    {
        neon_astparser_parseexpr(prs);
        neon_astparser_emit2byte(prs, NEON_OP_PROPERTYSET, name);
    }
    else if(neon_astparser_match(prs, NEON_TOK_PARENOPEN))
    {
        argc = neon_astparser_parsearglist(prs);
        neon_astparser_emit2byte(prs, NEON_OP_INSTTHISINVOKE, name);
        neon_astparser_emit1byte(prs, argc);
    }
    else
    {
        getop = NEON_OP_PROPERTYGET;
        setop = NEON_OP_PROPERTYSET;
        if((prs->currclass != NULL) && (previous.type == NEON_TOK_KWTHIS))
        {
            getop = NEON_OP_INSTTHISPROPERTYGET;
        }
        //neon_astparser_emit2byte(prs, NEON_OP_PROPERTYGET, name);
        neon_astparser_doassign(prs, getop, setop, name, canassign);
    }
}

void neon_astparser_ruleliteral(NeonAstParser* prs, bool canassign)
{
    (void)canassign;
    switch(prs->previous.type)
    {
        case NEON_TOK_KWFALSE:
            neon_astparser_emit1byte(prs, NEON_OP_PUSHFALSE);
            break;
        case NEON_TOK_KWNIL:
            neon_astparser_emit1byte(prs, NEON_OP_PUSHNIL);
            break;
        case NEON_TOK_KWTRUE:
            neon_astparser_emit1byte(prs, NEON_OP_PUSHTRUE);
            break;
        default:
            return;// Unreachable.
    }
}

void neon_astparser_rulegrouping(NeonAstParser* prs, bool canassign)
{
    (void)canassign;
    neon_astparser_ignorespace(prs);
    neon_astparser_parseexpr(prs);
    neon_astparser_ignorespace(prs);
    neon_astparser_consume(prs, NEON_TOK_PARENCLOSE, "expected ')' after grouped expression");
}

NeonValue neon_astparser_parsenumber(NeonAstParser* prs)
{
    double vdoub;
    long long vbin;
    long voct;
    long vhex;
    if(prs->previous.type == NEON_TOK_BINNUMBER)
    {
        vbin = strtoll(prs->previous.start + 2, NULL, 2);
        return neon_value_makenumber(vbin);
    }
    else if(prs->previous.type == NEON_TOK_OCTNUMBER)
    {
        voct = strtol(prs->previous.start + 2, NULL, 8);
        return neon_value_makenumber(voct);
    }
    else if(prs->previous.type == NEON_TOK_HEXNUMBER)
    {
        vhex = strtol(prs->previous.start, NULL, 16);
        return neon_value_makenumber(vhex);
    }
    vdoub = strtod(prs->previous.start, NULL);
    return neon_value_makenumber(vdoub);
}

void neon_astparser_rulenumber(NeonAstParser* prs, bool canassign)
{
    (void)canassign;
    neon_astparser_emitconstant(prs, neon_astparser_parsenumber(prs));
}

void neon_astparser_ruleor(NeonAstParser* prs, NeonAstToken previous, bool canassign)
{
    int endjump;
    int elsejump;
    (void)previous;
    (void)canassign;
    elsejump = neon_astparser_emitjump(prs, NEON_OP_JUMPIFFALSE);
    endjump = neon_astparser_emitjump(prs, NEON_OP_JUMPNOW);
    neon_astparser_emitpatchjump(prs, elsejump);
    neon_astparser_emit1byte(prs, NEON_OP_POPONE);
    neon_astparser_parseprec(prs, NEON_PREC_OR);
    neon_astparser_emitpatchjump(prs, endjump);
}

#define stringesc1(c, rpl1) \
    case c: \
        { \
            buf[pi] = rpl1; \
            pi += 1; \
            i += 1; \
        } \
        break;

void neon_astparser_rulestring(NeonAstParser* prs, bool canassign)
{
    size_t i;
    size_t pi;
    size_t rawlen;
    size_t nlen;
    int currc;
    int nextc;
    bool needalloc;
    char* buf;
    const char* rawstr;
    NeonObjString* os;
    (void)canassign;
    needalloc = false;
    rawstr = prs->previous.start + 1;
    rawlen = prs->previous.length - 2;
    // first, iterate, try to figure out how much more to allocate...
    nlen = rawlen;
    for(i=0; i<rawlen; i++)
    {
        currc = rawstr[i];
        if(currc == '\\')
        {
            needalloc = true;
            nlen += 1;
        }
    }
    if(needalloc)
    {
        buf = (char*)malloc(nlen + 1);
        memset(buf, 0, nlen+1);
        pi = 0;
        i = 0;
        while(i<rawlen)
        {
            currc = rawstr[i];
            nextc = -1;
            if((i + 1) < rawlen)
            {
                nextc = rawstr[i + 1];
            }
            if(currc == '\\')
            {
                {

                    switch(nextc)
                    {
                        stringesc1('\\', '\\');
                        stringesc1('0', '\0');
                        stringesc1('1', '\1');
                        stringesc1('2', '\2');
                        stringesc1('n', '\n');
                        stringesc1('t', '\t');
                        stringesc1('r', '\r');
                        stringesc1('e', '\e');
                        stringesc1('f', '\f');
                        stringesc1('"', '"');
                        default:
                            {
                                neon_astparser_raiseerror(prs, "unknown string escape character '%c' (%d)", nextc, nextc);
                            }
                            break;                    
                    }
                    #undef stringesc1
                }
            }
            else
            {
                buf[pi] = currc;
                pi += 1;
            }
            i++;
        }
        os = neon_string_take(prs->pvm, buf, pi);
    }
    else
    {
        os = neon_string_copy(prs->pvm, rawstr, rawlen);
    }
    neon_astparser_emitconstant(prs, neon_value_fromobject(os));

}

int neon_astparser_resolvelocal(NeonAstParser* prs, NeonAstCompiler* compiler, NeonAstToken* name)
{
    int i;
    NeonAstLocal* local;
    for(i = compiler->localcount - 1; i >= 0; i--)
    {
        local = &compiler->locals[i];
        if(neon_astparser_identsequal(name, &local->name))
        {
            if(local->depth == -1)
            {
                neon_astparser_raiseerror(prs, "cannot read local variable in its own initializer");
            }
            return i;
        }
    }
    return -1;
}

int neon_astparser_addupval(NeonAstParser* prs, NeonAstCompiler* compiler, int32_t index, bool islocal)
{
    int i;
    int upvaluecount;
    NeonAstUpvalue* upvalue;
    upvaluecount = compiler->currfunc->upvaluecount;
    for(i = 0; i < upvaluecount; i++)
    {
        upvalue = &compiler->compupvals[i];
        if(upvalue->index == index && upvalue->islocal == islocal)
        {
            return i;
        }
    }
    if(upvaluecount == NEON_MAX_COMPUPVALS)
    {
        neon_astparser_raiseerror(prs, "too many closure variables in function");
        return 0;
    }
    compiler->compupvals[upvaluecount].islocal = islocal;
    compiler->compupvals[upvaluecount].index = index;
    return compiler->currfunc->upvaluecount++;
}


int neon_astparser_resolveupval(NeonAstParser* prs, NeonAstCompiler* compiler, NeonAstToken* name)
{
    int localidx;
    int upvalue;
    if(compiler->enclosing == NULL)
    {
        return -1;
    }
    localidx = neon_astparser_resolvelocal(prs, compiler->enclosing, name);
    if(localidx != -1)
    {
        compiler->enclosing->locals[localidx].iscaptured = true;
        return neon_astparser_addupval(prs, compiler, (int32_t)localidx, true);
    }
    upvalue = neon_astparser_resolveupval(prs, compiler->enclosing, name);
    if(upvalue != -1)
    {
        return neon_astparser_addupval(prs, compiler, (int32_t)upvalue, false);
    }
    return -1;
}

void neon_astparser_parsenamedvar(NeonAstParser* prs, NeonAstToken name, bool canassign)
{
    int32_t getop;
    int32_t setop;
    int arg;
    (void)canassign;
    arg = neon_astparser_resolvelocal(prs, prs->currcompiler, &name);
    if(arg != -1)
    {
        getop = NEON_OP_LOCALGET;
        setop = NEON_OP_LOCALSET;
        return neon_astparser_doassign(prs, getop, setop, arg, canassign);
    }
    else
    {
        arg = neon_astparser_resolveupval(prs, prs->currcompiler, &name);
        if(arg != -1)
        {
            getop = NEON_OP_UPVALGET;
            setop = NEON_OP_UPVALSET;
            return neon_astparser_doassign(prs, getop, setop, arg, canassign);
        }
        else
        {
            arg = neon_astparser_makeidentconstant(prs, &name);
            getop = NEON_OP_GLOBALGET;
            setop = NEON_OP_GLOBALSET;
            return neon_astparser_doassign(prs, getop, setop, arg, canassign);
        }
    }
}

void neon_astparser_rulevariable(NeonAstParser* prs, bool canassign)
{
    neon_astparser_parsenamedvar(prs, prs->previous, canassign);
}

NeonAstToken neon_astparser_makesyntoken(NeonAstParser* prs, const char* text)
{
    NeonAstToken token;
    (void)prs;
    token.start = text;
    token.length = (int)strlen(text);
    return token;
}

void neon_astparser_rulesuper(NeonAstParser* prs, bool canassign)
{
    int32_t name;
    int32_t argc;
    (void)canassign;
    if(prs->currclass == NULL)
    {
        neon_astparser_raiseerror(prs, "cannot use 'super' outside of a class");
    }
    else if(!prs->currclass->hassuperclass)
    {
        neon_astparser_raiseerror(prs, "cannot use 'super' in a class with no superclass");
    }
    neon_astparser_consume(prs, NEON_TOK_DOT, "expected '.' after 'super'");
    neon_astparser_consume(prs, NEON_TOK_IDENTIFIER, "expected superclass method name");
    name = neon_astparser_makeidentconstant(prs, &prs->previous);
    
    neon_astparser_parsenamedvar(prs, neon_astparser_makesyntoken(prs, "this"), false);
    /* Superclasses super-get < Superclasses super-invoke */
    /*
    neon_astparser_parsenamedvar(prs, neon_astparser_makesyntoken(prs, "super"), false);
    neon_astparser_emit2byte(prs, NEON_OP_INSTGETSUPER, name);
    */
    if(neon_astparser_match(prs, NEON_TOK_PARENOPEN))
    {
        argc = neon_astparser_parsearglist(prs);
        neon_astparser_parsenamedvar(prs, neon_astparser_makesyntoken(prs, "super"), false);
        neon_astparser_emit2byte(prs, NEON_OP_INSTSUPERINVOKE, name);
        neon_astparser_emit1byte(prs, argc);
    }
    else
    {
        neon_astparser_parsenamedvar(prs, neon_astparser_makesyntoken(prs, "super"), false);
        neon_astparser_emit2byte(prs, NEON_OP_INSTGETSUPER, name);
    }
}

void neon_astparser_rulethis(NeonAstParser* prs, bool canassign)
{
    (void)canassign;
    if(prs->currclass == NULL)
    {
        neon_astparser_raiseerror(prs, "can't use 'this' outside of a class.");
        return;
    }
    neon_astparser_rulevariable(prs, false);
}

void neon_astparser_ruleunary(NeonAstParser* prs, bool canassign)
{
    NeonAstTokType ot;
    (void)canassign;
    ot = prs->previous.type;
    // Compile the operand.
    /* Compiling Expressions unary < Compiling Expressions unary-operand */
    //neon_astparser_parseexpr(prs);
    neon_astparser_parseprec(prs, NEON_PREC_UNARY);
    // Emit the operator instruction.
    switch(ot)
    {
        case NEON_TOK_EXCLAM:
            neon_astparser_emit1byte(prs, NEON_OP_PRIMNOT);
            break;
        case NEON_TOK_MINUS:
            neon_astparser_emit1byte(prs, NEON_OP_PRIMNEGATE);
            break;
        case NEON_TOK_TILDE:
            neon_astparser_emit1byte(prs, NEON_OP_PRIMBINNOT);
        default:
            return;// Unreachable.
    }
}

static void neon_astparser_parseassign(NeonAstParser* p, uint8_t realop, uint8_t getop, uint8_t setop, int arg)
{
    if(getop == NEON_OP_PROPERTYGET || getop == NEON_OP_INSTTHISPROPERTYGET)
    {
        neon_astparser_emit1byte(p, NEON_OP_DUP);
    }
    if(arg != -1)
    {
        neon_astparser_emit2byte(p, getop, arg);
    }
    else
    {
        neon_astparser_emit2byte(p, getop, 1);
    }
    neon_astparser_parseexpr(p);
    neon_astparser_emit1byte(p, realop);
    if(arg != -1)
    {
        neon_astparser_emit2byte(p, setop, (uint16_t)arg);
    }
    else
    {
        neon_astparser_emit1byte(p, setop);
    }
}

void neon_astparser_doassign(NeonAstParser* prs, int32_t getop, int32_t setop, int arg, bool canassign)
{
    if(canassign && neon_astparser_match(prs, NEON_TOK_ASSIGN))
    {
        neon_astparser_parseexpr(prs);
        neon_astparser_emit2byte(prs, setop, (int32_t)arg);
    }
    else if(canassign && neon_astparser_match(prs, NEON_TOK_ASSIGNPLUS))
    {
        neon_astparser_parseassign(prs, NEON_OP_PRIMADD, getop, setop, arg);
    }
    else if(canassign && neon_astparser_match(prs, NEON_TOK_ASSIGNMINUS))
    {
        neon_astparser_parseassign(prs, NEON_OP_PRIMSUBTRACT, getop, setop, arg);
    }
    else if(canassign && neon_astparser_match(prs, NEON_TOK_INCREMENT))
    {
        if(getop == NEON_OP_PROPERTYGET || getop == NEON_OP_INSTTHISPROPERTYGET)
        {
            neon_astparser_emit1byte(prs, NEON_OP_DUP);
        }
        if(arg != -1)
        {
            neon_astparser_emit2byte(prs, getop, arg);
        }
        else
        {
            neon_astparser_emit2byte(prs, getop, 1);
        }
        neon_astparser_emit2byte(prs, NEON_OP_PUSHONE, NEON_OP_PRIMADD);
        neon_astparser_emit2byte(prs, setop, arg);
    }
    else if(canassign && neon_astparser_match(prs, NEON_TOK_DECREMENT))
    {
        if(arg != -1)
        {
            neon_astparser_emit2byte(prs, getop, arg);
        }
        else
        {
            neon_astparser_emit2byte(prs, getop, 1);
        }
        neon_astparser_emit2byte(prs, NEON_OP_PUSHONE, NEON_OP_PRIMSUBTRACT);
        neon_astparser_emit2byte(prs, setop, arg);
    }
    else
    {
        if(arg != -1)
        {
            if(getop == NEON_OP_INDEXGET)
            {
                neon_astparser_emit2byte(prs, getop, (int32_t)canassign);
            }
            else
            {
                neon_astparser_emit2byte(prs, getop, arg);
            }
        }
        else
        {
            neon_astparser_emit2byte(prs, getop, (int32_t)arg);
        }
    }
}

void neon_astparser_rulearray(NeonAstParser* prs, bool canassign)
{
    int count;
    (void)canassign;
    count = 0;
    if(!neon_astparser_checkcurrent(prs, NEON_TOK_BRACKETCLOSE))
    {
        do
        {
            neon_astparser_parseexpr(prs);
            count++;
        } while(neon_astparser_match(prs, NEON_TOK_COMMA));
    }
    neon_astparser_consume(prs, NEON_TOK_BRACKETCLOSE, "expected ']' at end of list literal");
    neon_astparser_emit2byte(prs, NEON_OP_MAKEARRAY, count);
}

void neon_astparser_ruleindex(NeonAstParser* prs, NeonAstToken previous, bool canassign)
{
    bool willassign;
    (void)previous;
    (void)willassign;
    willassign = false;
    neon_astparser_parseexpr(prs);
    neon_astparser_consume(prs, NEON_TOK_BRACKETCLOSE, "expected ']' after indexing");
    if(neon_astparser_checkcurrent(prs, NEON_TOK_ASSIGN))
    {
        willassign = true;
    }
    neon_astparser_doassign(prs, NEON_OP_INDEXGET, NEON_OP_INDEXSET, -1, canassign);
}

void neon_astparser_rulemap(NeonAstParser* prs, bool canassign)
{
    int count;
    (void)canassign;
    count = 0;
    neon_astparser_consume(prs, NEON_TOK_BRACECLOSE, "expected '}' at end of map literal");
    neon_astparser_emit2byte(prs, NEON_OP_MAKEMAP, count);
}

void neon_astparser_ruleglobalstmt(NeonAstParser* prs, bool canassign)
{
    int iv;
    (void)canassign;
    iv = -1;
    if(neon_astparser_match(prs, NEON_TOK_DOT))
    {
        neon_astparser_consume(prs, NEON_TOK_IDENTIFIER, "expect name after '.'");
        iv = neon_astparser_makeidentconstant(prs, &prs->previous);
        neon_astparser_emit2byte(prs, NEON_OP_GLOBALSTMT, iv);
    }
    else
    {
        neon_astparser_emit2byte(prs, NEON_OP_GLOBALSTMT, -1);
        if(neon_astparser_match(prs, NEON_TOK_BRACKETOPEN))
        {
            neon_astparser_ruleindex(prs, prs->previous, true);
            //neon_astparser_doassign(prs, NEON_OP_INDEXGET, NEON_OP_INDEXSET, -1, true);
        }
    }
    neon_astparser_skipsemicolon(prs);
}

void neon_astparser_ruletypeof(NeonAstParser* prs, bool canassign)
{
    (void)canassign;
    neon_astparser_parseexpr(prs);
    neon_astparser_emit1byte(prs, NEON_OP_TYPEOF);
    neon_astparser_skipsemicolon(prs);
}

void neon_astparser_rulefunction(NeonAstParser* prs, bool canassign)
{
    char namebuf[128];
    NeonAstCompiler compiler;
    (void)canassign;
    neon_astparser_advance(prs);
    sprintf(namebuf, "lambda@%d", prs->current.line);
    neon_astparser_compilerinit(prs, &compiler, NEON_TYPE_FUNCTION, namebuf);
    neon_astparser_scopebegin(prs);
    neon_astparser_parsefunctionbody(prs, &compiler, true);
    //neon_astparser_emit1byte(prs, NEON_OP_POPONE);
}

void neon_astparser_rulenew(NeonAstParser* prs, bool canassign)
{
    neon_astparser_consume(prs, NEON_TOK_IDENTIFIER, "class name after 'new'");
    neon_astparser_rulevariable(prs, canassign);
}

NeonAstRule* neon_astparser_setrule(NeonAstRule* rule, NeonAstParsePrefixFN prefix, NeonAstParseInfixFN infix, NeonAstPrecedence precedence)
{
    rule->prefix = prefix;
    rule->infix = infix;
    rule->precedence = precedence;
    return rule;
}

NeonAstRule* neon_astparser_getrule(NeonAstTokType type)
{
    static NeonAstRule dest;
    switch(type)
    {
        case NEON_TOK_NEWLINE:
            {
                return neon_astparser_setrule(&dest, NULL, NULL, NEON_PREC_NONE);
            }
            break;
        case NEON_TOK_PARENOPEN:
            {
                return neon_astparser_setrule(&dest,  neon_astparser_rulegrouping, neon_astparser_rulecall, NEON_PREC_CALL );
            }
            break;
        case NEON_TOK_PARENCLOSE:
            {
                return neon_astparser_setrule(&dest,  NULL, NULL, NEON_PREC_NONE );
            }
            break;
        case NEON_TOK_BRACEOPEN:
            {
                return neon_astparser_setrule(&dest, neon_astparser_rulemap, NULL, NEON_PREC_NONE );
            }
            break;
        case NEON_TOK_BRACECLOSE:
            {
                return neon_astparser_setrule(&dest,  NULL, NULL, NEON_PREC_NONE );
            }
            break;
        case NEON_TOK_BRACKETOPEN:
            {
                return neon_astparser_setrule(&dest,  neon_astparser_rulearray, neon_astparser_ruleindex, NEON_PREC_CALL );
            }
            break;
        case NEON_TOK_BRACKETCLOSE:
            {
                return neon_astparser_setrule(&dest,  NULL, NULL, NEON_PREC_NONE );
            }
            break;
        case NEON_TOK_COMMA:
            {
                return neon_astparser_setrule(&dest,  NULL, NULL, NEON_PREC_NONE );
            }
            break;
        /* Compiling Expressions rules < Classes and Instances table-dot */
        // [NEON_TOK_DOT]           = {NULL,     NULL,   NEON_PREC_NONE},
        case NEON_TOK_DOT:
            {
                return neon_astparser_setrule(&dest,  NULL, neon_astparser_ruledot, NEON_PREC_CALL );
            }
            break;
        case NEON_TOK_MINUS:
            {
                return neon_astparser_setrule(&dest,  neon_astparser_ruleunary, neon_astparser_rulebinary, NEON_PREC_TERM );
            }
            break;
        case NEON_TOK_PLUS:
            {
                return neon_astparser_setrule(&dest,  NULL, neon_astparser_rulebinary, NEON_PREC_TERM );
            }
            break;
        case NEON_TOK_SEMICOLON:
            {
                return neon_astparser_setrule(&dest,  NULL, NULL, NEON_PREC_NONE );
            }
            break;
        case NEON_TOK_DIVIDE:
            {
                return neon_astparser_setrule(&dest,  NULL, neon_astparser_rulebinary, NEON_PREC_FACTOR );
            }
            break;
        case NEON_TOK_MULTIPLY:
            {
                return neon_astparser_setrule(&dest,  NULL, neon_astparser_rulebinary, NEON_PREC_FACTOR );
            }
            break;
        case NEON_TOK_MODULO:
            {
                return neon_astparser_setrule(&dest,  NULL, neon_astparser_rulebinary, NEON_PREC_FACTOR );
            }
            break;
        case NEON_TOK_BINAND:
            {
                return neon_astparser_setrule(&dest,  NULL, neon_astparser_rulebinary, NEON_PREC_FACTOR );
            }
            break;
        case NEON_TOK_BINOR:
            {
                return neon_astparser_setrule(&dest,  NULL, neon_astparser_rulebinary, NEON_PREC_FACTOR );
            }
            break;
        case NEON_TOK_BINXOR:
            {
                return neon_astparser_setrule(&dest,  NULL, neon_astparser_rulebinary, NEON_PREC_FACTOR );
            }
            break;
        case NEON_TOK_SHIFTLEFT:
            {
                return neon_astparser_setrule(&dest, NULL, neon_astparser_rulebinary, NEON_PREC_SHIFT);
            }
            break;
        case NEON_TOK_SHIFTRIGHT:
            {
                return neon_astparser_setrule(&dest, NULL, neon_astparser_rulebinary, NEON_PREC_SHIFT);
            }
            break;
        case NEON_TOK_INCREMENT:
            {
                return neon_astparser_setrule(&dest, NULL, NULL, NEON_PREC_NONE);
            }
            break;
        case NEON_TOK_DECREMENT:
            {
                return neon_astparser_setrule(&dest, NULL, NULL, NEON_PREC_NONE);
            }
            break;
        /* Compiling Expressions rules < Types of Values table-not */
        // [NEON_TOK_EXCLAM]          = {NULL,     NULL,   NEON_PREC_NONE},
        case NEON_TOK_EXCLAM:
            {
                return neon_astparser_setrule(&dest,  neon_astparser_ruleunary, NULL, NEON_PREC_NONE );
            }
            break;
        case NEON_TOK_TILDE:
            {
                return neon_astparser_setrule(&dest, neon_astparser_ruleunary, NULL, NEON_PREC_UNARY);
            }
            break;
        /* Compiling Expressions rules < Types of Values table-equal */
        // [NEON_TOK_COMPNOTEQUAL]    = {NULL,     NULL,   NEON_PREC_NONE},
        case NEON_TOK_COMPNOTEQUAL:
            {
                return neon_astparser_setrule(&dest,  NULL, neon_astparser_rulebinary, NEON_PREC_EQUALITY );
            }
            break;
        case NEON_TOK_ASSIGN:
        case NEON_TOK_ASSIGNPLUS:
        case NEON_TOK_ASSIGNMINUS:
        case NEON_TOK_ASSIGNMULT:
        case NEON_TOK_ASSIGNMODULO:
        case NEON_TOK_ASSIGNDIV:
            {
                return neon_astparser_setrule(&dest,  NULL, NULL, NEON_PREC_NONE );
            }
            break;
        /* Compiling Expressions rules < Types of Values table-comparisons */
        // [NEON_TOK_COMPEQUAL]   = {NULL,     NULL,   NEON_PREC_NONE},
        // [NEON_TOK_COMPGREATERTHAN]       = {NULL,     NULL,   NEON_PREC_NONE},
        // [NEON_TOK_COMPGREATEREQUAL] = {NULL,     NULL,   NEON_PREC_NONE},
        // [NEON_TOK_COMPLESSTHAN]          = {NULL,     NULL,   NEON_PREC_NONE},
        // [NEON_TOK_COMPLESSEQUAL]    = {NULL,     NULL,   NEON_PREC_NONE},
        case NEON_TOK_COMPEQUAL:
            {
                return neon_astparser_setrule(&dest,  NULL, neon_astparser_rulebinary, NEON_PREC_EQUALITY );
            }
            break;
        case NEON_TOK_COMPGREATERTHAN:
            {
                return neon_astparser_setrule(&dest,  NULL, neon_astparser_rulebinary, NEON_PREC_COMPARISON );
            }
            break;
        case NEON_TOK_COMPGREATEREQUAL:
            {
                return neon_astparser_setrule(&dest,  NULL, neon_astparser_rulebinary, NEON_PREC_COMPARISON );
            }
            break;
        case NEON_TOK_COMPLESSTHAN:
            {
                return neon_astparser_setrule(&dest,  NULL, neon_astparser_rulebinary, NEON_PREC_COMPARISON );
            }
            break;
        case NEON_TOK_COMPLESSEQUAL:
            {
                return neon_astparser_setrule(&dest,  NULL, neon_astparser_rulebinary, NEON_PREC_COMPARISON );
            }
            break;
        /* Compiling Expressions rules < Global Variables table-identifier */
        // [NEON_TOK_IDENTIFIER]    = {NULL,     NULL,   NEON_PREC_NONE},
        case NEON_TOK_IDENTIFIER:
            {
                return neon_astparser_setrule(&dest,  neon_astparser_rulevariable, NULL, NEON_PREC_NONE );
            }
            break;
        /* Compiling Expressions rules < Strings table-string */
        // [NEON_TOK_STRING]        = {NULL,     NULL,   NEON_PREC_NONE},
        case NEON_TOK_STRING:
            {
                return neon_astparser_setrule(&dest,  neon_astparser_rulestring, NULL, NEON_PREC_NONE );
            }
            break;
        case NEON_TOK_NUMBER:
        case NEON_TOK_OCTNUMBER:
        case NEON_TOK_HEXNUMBER:
        case NEON_TOK_BINNUMBER:
            {
                return neon_astparser_setrule(&dest,  neon_astparser_rulenumber, NULL, NEON_PREC_NONE );
            }
            break;
        /* Compiling Expressions rules < Jumping Back and Forth table-and */
        // [NEON_TOK_KWAND]           = {NULL,     NULL,   NEON_PREC_NONE},

        case NEON_TOK_KWNEW:
            {
                return neon_astparser_setrule(&dest, neon_astparser_rulenew, NULL, NEON_PREC_NONE);
            }
            break;
        case NEON_TOK_KWBREAK:
            {
                return neon_astparser_setrule(&dest, NULL, NULL, NEON_PREC_NONE);
            }
            break;
        case NEON_TOK_KWCONTINUE:
            {
                return neon_astparser_setrule(&dest,  NULL, NULL, NEON_PREC_NONE );
            }
            break;
        case NEON_TOK_KWAND:
            {
                return neon_astparser_setrule(&dest,  NULL, neon_astparser_ruleand, NEON_PREC_AND );
            }
            break;
        case NEON_TOK_KWCLASS:
            {
                return neon_astparser_setrule(&dest,  NULL, NULL, NEON_PREC_NONE );
            }
            break;
        case NEON_TOK_KWELSE:
            {
                return neon_astparser_setrule(&dest,  NULL, NULL, NEON_PREC_NONE );
            }
            break;
        /* Compiling Expressions rules < Types of Values table-false */
        // [NEON_TOK_KWFALSE]         = {NULL,     NULL,   NEON_PREC_NONE},
        case NEON_TOK_KWFALSE:
            {
                return neon_astparser_setrule(&dest,  neon_astparser_ruleliteral, NULL, NEON_PREC_NONE );
            }
            break;
        case NEON_TOK_KWFOR:
            {
                return neon_astparser_setrule(&dest,  NULL, NULL, NEON_PREC_NONE );
            }
            break;
        case NEON_TOK_KWFUNCTION:
            {
                return neon_astparser_setrule(&dest, neon_astparser_rulefunction, NULL, NEON_PREC_NONE );
            }
            break;
        case NEON_TOK_KWIF:
            {
                return neon_astparser_setrule(&dest,  NULL, NULL, NEON_PREC_NONE );
            }
            break;
        /* Compiling Expressions rules < Types of Values table-nil
        * [NEON_TOK_KWNIL]           = {NULL,     NULL,   NEON_PREC_NONE},
        */
        case NEON_TOK_KWNIL:
            {
                return neon_astparser_setrule(&dest,  neon_astparser_ruleliteral, NULL, NEON_PREC_NONE );
            }
            break;
        /* Compiling Expressions rules < Jumping Back and Forth table-or
        * [NEON_TOK_KWOR]            = {NULL,     NULL,   NEON_PREC_NONE},
        */
        case NEON_TOK_KWOR:
            {
                return neon_astparser_setrule(&dest,  NULL, neon_astparser_ruleor, NEON_PREC_OR );
            }
            break;
        case NEON_TOK_KWDEBUGPRINT:
            {
                return neon_astparser_setrule(&dest,  NULL, NULL, NEON_PREC_NONE );
            }
            break;
        case NEON_TOK_KWGLOBAL:
            {
                return neon_astparser_setrule(&dest, neon_astparser_ruleglobalstmt, NULL, NEON_PREC_NONE );
            }
            break;
        case NEON_TOK_KWTYPEOF:
            {
                return neon_astparser_setrule(&dest, neon_astparser_ruletypeof, NULL, NEON_PREC_NONE);
            }
            break;
        case NEON_TOK_KWRETURN:
            {
                return neon_astparser_setrule(&dest,  NULL, NULL, NEON_PREC_NONE );
            }
            break;
        /* Compiling Expressions rules < Superclasses table-super
        * [NEON_TOK_KWSUPER]         = {NULL,     NULL,   NEON_PREC_NONE},
        */
        case NEON_TOK_KWSUPER:
            {
                return neon_astparser_setrule(&dest,  neon_astparser_rulesuper, NULL, NEON_PREC_NONE );
            }
            break;
        /* Compiling Expressions rules < Methods and Initializers table-this
        * [NEON_TOK_KWTHIS]          = {NULL,     NULL,   NEON_PREC_NONE},
        */
        case NEON_TOK_KWTHIS:
            {
                return neon_astparser_setrule(&dest,  neon_astparser_rulethis, NULL, NEON_PREC_NONE );
            }
            break;
        /* Compiling Expressions rules < Types of Values table-true
        * [NEON_TOK_KWTRUE]          = {NULL,     NULL,   NEON_PREC_NONE},
        */
        case NEON_TOK_KWTRUE:
            {
                return neon_astparser_setrule(&dest,  neon_astparser_ruleliteral, NULL, NEON_PREC_NONE );
            }
            break;
        case NEON_TOK_KWVAR:
            {
                return neon_astparser_setrule(&dest,  NULL, NULL, NEON_PREC_NONE );
            }
            break;
        case NEON_TOK_KWWHILE:
            {
                return neon_astparser_setrule(&dest,  NULL, NULL, NEON_PREC_NONE );
            }
            break;
        case NEON_TOK_ERROR:
            {
                return neon_astparser_setrule(&dest,  NULL, NULL, NEON_PREC_NONE );
            }
            break;
        case NEON_TOK_EOF:
            {
                return neon_astparser_setrule(&dest,  NULL, NULL, NEON_PREC_NONE );
            }
            break;
    }
    return NULL;
}

void neon_astparser_ignorespace(NeonAstParser* prs)
{
    while(neon_astparser_match(prs, NEON_TOK_NEWLINE))
    {
    }
}



    void neon_astparser_parseprec(NeonAstParser* prs, NeonAstPrecedence precedence)
    {
        bool canassign;
        NeonAstRule* rule;
        NeonAstToken previous;
        NeonAstParseInfixFN infixrule;
        NeonAstParsePrefixFN prefixrule;
        neon_astparser_advance(prs);
        prefixrule = neon_astparser_getrule(prs->previous.type)->prefix;
        if(prefixrule == NULL)
        {
            neon_astparser_raiseerror(prs, "expected expression");
            return;
        }
        canassign = precedence <= NEON_PREC_ASSIGNMENT;
        prefixrule(prs, canassign);
        /*
        while(precedence <= neon_astparser_getrule(prs->current.type)->precedence)
        {
            previous = prs->previous;
            neon_astparser_advance(prs);
            infixrule = neon_astparser_getrule(prs->previous.type)->infix;
            infixrule(prs, previous, canassign);
        }
        */


        while(true)
        {
            rule = neon_astparser_getrule(prs->current.type);
            if(rule == NULL)
            {
                break;
            }
            if(precedence <= rule->precedence)
            {
                previous = prs->previous;
                neon_astparser_advance(prs);
                infixrule = neon_astparser_getrule(prs->previous.type)->infix;
                infixrule(prs, previous, canassign);
            }
            else
            {
                break;
            }
        }


        if(canassign && neon_astparser_match(prs, NEON_TOK_ASSIGN))
        {
            neon_astparser_raiseerror(prs, "invalid assignment target");
        }
    }

void neon_astparser_parseprecnoadvance(NeonAstParser* p, NeonAstPrecedence precedence)
{
    if(neon_astlex_isatend(p->pscn))
    {
        return;
    }
    neon_astparser_ignorespace(p);
    if(neon_astlex_isatend(p->pscn))
    {
        return;
    }
    neon_astparser_parseprec(p, precedence);
}



void neon_astparser_parseexpr(NeonAstParser* prs)
{
    /* Compiling Expressions expression < Compiling Expressions expression-body
    // What goes here?
    */
    neon_astparser_parseprec(prs, NEON_PREC_ASSIGNMENT);
}

void neon_astparser_parseblock(NeonAstParser* prs)
{
    while(!neon_astparser_checkcurrent(prs, NEON_TOK_BRACECLOSE) && !neon_astparser_checkcurrent(prs, NEON_TOK_EOF))
    {
        neon_astparser_parsedecl(prs);
    }
    neon_astparser_consume(prs, NEON_TOK_BRACECLOSE, "expected '}' after block");
    neon_astparser_skipsemicolon(prs);
}

void neon_astparser_parsefunctionargs(NeonAstParser* prs, bool anon)
{
    int32_t constant;
    if(!anon)
    {
        neon_astparser_consume(prs, NEON_TOK_PARENOPEN, "expected '(' after function name");
    }
    if(!neon_astparser_checkcurrent(prs, NEON_TOK_PARENCLOSE))
    {
        do
        {
            prs->currcompiler->currfunc->arity++;
            if(prs->currcompiler->currfunc->arity > 255)
            {
                neon_astparser_raiseatcurrent(prs, "function has too many arguments declared");
            }
            constant = neon_astparser_parsevarname(prs, "expected parameter name");
            neon_astparser_emitdefvar(prs, constant);
        } while(neon_astparser_match(prs, NEON_TOK_COMMA));
    }
    neon_astparser_consume(prs, NEON_TOK_PARENCLOSE, "expected ')' after parameters");
}

void neon_astparser_parsefunctionbody(NeonAstParser* prs, NeonAstCompiler* compiler, bool anon)
{
    int i;
    NeonObjScriptFunction* fn;
    neon_astparser_scopebegin(prs);// [no-end-scope]
    neon_astparser_parsefunctionargs(prs, anon);
    neon_astparser_consume(prs, NEON_TOK_BRACEOPEN, "expected '{' before function body");
    neon_astparser_parseblock(prs);
    fn = neon_astparser_compilerfinish(prs, false);
    /* Calls and Functions compile-function < Closures emit-closure */
    // neon_astparser_emit2byte(prs, NEON_OP_PUSHCONST, neon_astparser_makeconstant(prs, neon_value_fromobject(fn)));
    neon_astparser_emit2byte(prs, NEON_OP_CLOSURE, neon_astparser_makeconstant(prs, neon_value_fromobject(fn)));
    for(i = 0; i < fn->upvaluecount; i++)
    {
        neon_astparser_emit1byte(prs, compiler->compupvals[i].islocal ? 1 : 0);
        neon_astparser_emit1byte(prs, compiler->compupvals[i].index);
    }
}

void neon_astparser_parsemethod(NeonAstParser* prs)
{
    size_t sn;
    size_t prevlen;
    int32_t constant;
    const char* sc;
    NeonAstFuncType type;
    NeonAstCompiler compiler;
    neon_astparser_consume(prs, NEON_TOK_IDENTIFIER, "expect method name.");
    constant = neon_astparser_makeidentconstant(prs, &prs->previous);

    /* Methods and Initializers method-body < Methods and Initializers method-type */
    //type = NEON_TYPE_FUNCTION;
    type = NEON_TYPE_METHOD;
    sc = "constructor";
    sn = strlen(sc);
    prevlen = prs->previous.length;
    if((prevlen == sn) && memcmp(prs->previous.start, sc, sn) == 0)
    {
        type = NEON_TYPE_INITIALIZER;
    }
    neon_astparser_compilerinit(prs, &compiler, type, NULL);
    neon_astparser_parsefunctionbody(prs, &compiler, false);
    neon_astparser_emit2byte(prs, NEON_OP_METHOD, constant);
}

void neon_astparser_parseclassdecl(NeonAstParser* prs)
{
    int32_t nameconstant;
    NeonAstToken classname;
    NeonAstClassCompiler classcompiler;
    neon_astparser_consume(prs, NEON_TOK_IDENTIFIER, "expect class name.");
    classname = prs->previous;
    nameconstant = neon_astparser_makeidentconstant(prs, &prs->previous);
    neon_astparser_parsevarident(prs);
    neon_astparser_emit2byte(prs, NEON_OP_CLASS, nameconstant);
    neon_astparser_emitdefvar(prs, nameconstant);
    classcompiler.hassuperclass = false;
    classcompiler.enclosing = prs->currclass;
    prs->currclass = &classcompiler;
    if(neon_astparser_match(prs, NEON_TOK_COMPLESSTHAN))
    {
        neon_astparser_consume(prs, NEON_TOK_IDENTIFIER, "expect superclass name");
        neon_astparser_rulevariable(prs, false);
        if(neon_astparser_identsequal(&classname, &prs->previous))
        {
            neon_astparser_raiseerror(prs, "a class cannot inherit from itself");
        }
        neon_astparser_scopebegin(prs);
        neon_astparser_addlocal(prs, neon_astparser_makesyntoken(prs, "super"));
        neon_astparser_emitdefvar(prs, 0);
        neon_astparser_parsenamedvar(prs, classname, false);
        neon_astparser_emit1byte(prs, NEON_OP_INHERIT);
        classcompiler.hassuperclass = true;
    }
    neon_astparser_parsenamedvar(prs, classname, false);
    neon_astparser_consume(prs, NEON_TOK_BRACEOPEN, "expected '{' before class body");
    while(!neon_astparser_checkcurrent(prs, NEON_TOK_BRACECLOSE) && !neon_astparser_checkcurrent(prs, NEON_TOK_EOF))
    {
        neon_astparser_parsemethod(prs);
    }
    neon_astparser_consume(prs, NEON_TOK_BRACECLOSE, "expected '}' after class body");
    neon_astparser_emit1byte(prs, NEON_OP_POPONE);
    if(classcompiler.hassuperclass)
    {
        neon_astparser_scopeend(prs);
    }
    prs->currclass = prs->currclass->enclosing;
}

void neon_astparser_parsefuncdecl(NeonAstParser* prs)
{
    int32_t global;
    NeonAstCompiler compiler;
    global = neon_astparser_parsevarname(prs, "expected function name");
    neon_astparser_markinit(prs);
    neon_astparser_compilerinit(prs, &compiler, NEON_TYPE_FUNCTION, NULL);
    neon_astparser_parsefunctionbody(prs, &compiler, false);
    neon_astparser_emitdefvar(prs, global);
}

/*
void neon_astparser_parsevardecl(NeonAstParser* prs)
{
    int32_t global;
    global = neon_astparser_parsevarname(prs, "expected variable name");
    if(neon_astparser_match(prs, NEON_TOK_ASSIGN))
    {
        neon_astparser_parseexpr(prs);
    }
    else
    {
        neon_astparser_emit1byte(prs, NEON_OP_PUSHNIL);
    }
    neon_astparser_skipsemicolon(prs);
    neon_astparser_emitdefvar(prs, global);
}
*/

void neon_astparser_compilevardecl(NeonAstParser* p, bool isinitializer)
{
    int totalparsed = 0;
    do
    {
        if(totalparsed > 0)
        {
            neon_astparser_ignorespace(p);
        }
        int global = neon_astparser_parsevarname(p, "variable name expected");
        if(neon_astparser_match(p, NEON_TOK_ASSIGN))
        {
            neon_astparser_parseexpr(p);
        }
        else
        {
            neon_astparser_emit1byte(p, NEON_OP_PUSHNIL);
        }
        neon_astparser_emitdefvar(p, global);
        totalparsed++;
    } while(neon_astparser_match(p, NEON_TOK_COMMA));
    if(!isinitializer)
    {
        neon_astparser_consumestmtend(p);
    }
    else
    {
        neon_astparser_consume(p, NEON_TOK_SEMICOLON, "expected ';' after initializer");
        neon_astparser_ignorespace(p);
    }
}


void neon_astparser_parsevardecl(NeonAstParser* p)
{
    neon_astparser_compilevardecl(p, false);
}

void neon_astparser_consumestmtend(NeonAstParser* p)
{
    // allow block last statement to omit statement end
    if(p->blockcount > 0 && neon_astparser_checkcurrent(p, NEON_TOK_BRACECLOSE))
    {
        return;
    }
    if(neon_astparser_match(p, NEON_TOK_SEMICOLON))
    {
        while(neon_astparser_match(p, NEON_TOK_SEMICOLON) || neon_astparser_match(p, NEON_TOK_NEWLINE))
        {
            ;
        }
        return;
    }
    if(neon_astparser_match(p, NEON_TOK_EOF) || p->previous.type == NEON_TOK_EOF)
    {
        return;
    }
    //neon_astparser_consume(p, NEON_TOK_NEWLINE, "end of statement expected");
    while(neon_astparser_match(p, NEON_TOK_SEMICOLON) || neon_astparser_match(p, NEON_TOK_NEWLINE))
    {
        ;
    }
}

int32_t neon_astparser_parsevarname(NeonAstParser* prs, const char* errormessage)
{
    neon_astparser_consume(prs, NEON_TOK_IDENTIFIER, errormessage);
    neon_astparser_parsevarident(prs);
    if(prs->currcompiler->scopedepth > 0)
    {
        return 0;
        return 0;
    }
    return neon_astparser_makeidentconstant(prs, &prs->previous);
}


/*
void neon_astparser_parseexprstmt(NeonAstParser* prs)
{
    neon_astparser_parseexpr(prs);
    neon_astparser_skipsemicolon(prs);
    if(!prs->iseval)
    {
        neon_astparser_emit1byte(prs, NEON_OP_POPONE);
    }
}
*/

void neon_astparser_parseexprstmt(NeonAstParser* p, bool isinitializer, bool semi)
{
    if(p->currcompiler->scopedepth == 0)
    {
        //p->replcanecho = true;
    }
    if(!semi)
    {
        neon_astparser_parseexpr(p);
    }
    else
    {
        neon_astparser_parseprecnoadvance(p, NEON_PREC_ASSIGNMENT);
    }
    if(!isinitializer)
    {
        //if(p->replcanecho && p->pvm->isrepl)
        if(false)
        {
            //neon_astparser_emit1byte(p, NEON_OP_ECHO);
            //p->replcanecho = false;
        }
        else
        {
            neon_astparser_emit1byte(p, NEON_OP_POPONE);
        }
        neon_astparser_consumestmtend(p);
    }
    else
    {
        neon_astparser_consume(p, NEON_TOK_SEMICOLON, "expected ';' after initializer");
        neon_astparser_ignorespace(p);
        neon_astparser_emit1byte(p, NEON_OP_POPONE);
    }
}

/*
void neon_astparser_parseforstmt(NeonAstParser* p)
{
    neon_astparser_consume(p, NEON_TOK_PARENOPEN, "expected '(' after 'for' keyword");
    neon_astparser_scopebegin(p);
    // parse initializer...
    if(neon_astparser_match(p, NEON_TOK_SEMICOLON))
    {
        // no initializer
    }
    else if(neon_astparser_match(p, NEON_TOK_KWVAR))
    {
        neon_astparser_compilevardecl(p, true);
    }
    else
    {
        neon_astparser_parseexprstmt(p, true, false);
    }
    // keep a copy of the surrounding loop's start and depth
    int surroundingloopstart = p->innermostloopstart;
    int surroundingscopedepth = p->innermostloopscopedepth;
    // update the parser's loop start and depth to the current
    p->innermostloopstart = neon_astparser_currentblob(p)->count;
    p->innermostloopscopedepth = p->currcompiler->scopedepth;
    int exitjump = -1;
    if(!neon_astparser_match(p, NEON_TOK_SEMICOLON))
    {// the condition is optional
        neon_astparser_parseexpr(p);
        neon_astparser_consume(p, NEON_TOK_SEMICOLON, "expected ';' after condition");
        neon_astparser_ignorespace(p);
        // jump out of the loop if the condition is false...
        exitjump = neon_astparser_emitjump(p, NEON_OP_JUMPIFFALSE);
        neon_astparser_emit1byte(p, NEON_OP_POPONE);// pop the condition
    }
    // the iterator...
    if(!neon_astparser_checkcurrent(p, NEON_TOK_BRACEOPEN))
    {
        int bodyjump = neon_astparser_emitjump(p, NEON_OP_JUMPNOW);
        int incrementstart = neon_astparser_currentblob(p)->count;
        neon_astparser_parseexpr(p);
        neon_astparser_ignorespace(p);
        neon_astparser_emit1byte(p, NEON_OP_POPONE);
        neon_astparser_emitloop(p, p->innermostloopstart);
        p->innermostloopstart = incrementstart;
        neon_astparser_emitpatchjump(p, bodyjump);
    }
    neon_astparser_consume(p, NEON_TOK_PARENCLOSE, "expected ')' after 'for' conditionals");
    neon_astparser_parsestmt(p);
    neon_astparser_emitloop(p, p->innermostloopstart);
    if(exitjump != -1)
    {
        neon_astparser_emitpatchjump(p, exitjump);
        neon_astparser_emit1byte(p, NEON_OP_POPONE);
    }
    neon_astparser_endloop(p);
    // reset the loop start and scope depth to the surrounding value
    p->innermostloopstart = surroundingloopstart;
    p->innermostloopscopedepth = surroundingscopedepth;
    neon_astparser_scopeend(p);
}
*/


void neon_astparser_parseforstmt(NeonAstParser* prs)
{
    neon_astparser_scopebegin(prs);
    neon_astparser_consume(prs, NEON_TOK_PARENOPEN, "Expect '(' after 'for'");

    bool constant = false;
    if(neon_astparser_match(prs, NEON_TOK_KWVAR) /*|| (constant = neon_astparser_match(prs, NEON_TOK_KWCONST))*/)
    {
        neon_astparser_consume(prs, NEON_TOK_IDENTIFIER, "Expect variable name");
        NeonAstToken var = prs->previous;
        /*
        if(neon_astparser_checkcurrent(prs, NEON_TOK_KWIN) || neon_astparser_checkcurrent(prs, NEON_TOK_COMMA))
        {
            //for_in_statement(prs, var, constant);
            neon_astparser_raiseerror(prs, "for(... in...) not implemented yet");
            return;
        }
        */
        uint8_t global = neon_astparser_makeidentconstant(prs, &prs->previous);
        neon_astparser_parsenamedvar(prs, var, true);
        if(neon_astparser_match(prs, NEON_TOK_ASSIGN))
        {
            neon_astparser_parseexpr(prs);
        }
        else
        {
            neon_astparser_emit1byte(prs, NEON_OP_PUSHNIL);
        }

        neon_astparser_emitdefvar(prs, global/*, constant*/);
        neon_astparser_consume(prs, NEON_TOK_SEMICOLON, "Expect ';' after loop variable");
    }
    else
    {
        neon_astparser_parseexprstmt(prs, true, false);
        neon_astparser_consume(prs, NEON_TOK_SEMICOLON, "Expect ';' after loop expression");
    }

    NeonAstLoop loop;
    neon_astparser_beginloop(prs, &loop);
    prs->currcompiler->loop->end = -1;
    neon_astparser_parseexpr(prs);
    neon_astparser_consume(prs, NEON_TOK_SEMICOLON, "Expect ';' after loop condition");

    prs->currcompiler->loop->end = neon_astparser_emitjump(prs, NEON_OP_JUMPIFFALSE);
    neon_astparser_emit1byte(prs, NEON_OP_POPONE); /* Condition */

    int body_jump = neon_astparser_emitjump(prs, NEON_OP_JUMPNOW);

    int increment_start = neon_astparser_currentblob(prs)->count;
    neon_astparser_parseexpr(prs);
    neon_astparser_emit1byte(prs, NEON_OP_POPONE);
    neon_astparser_consume(prs, NEON_TOK_PARENCLOSE, "Expect ')' after for clauses");

    neon_astparser_emitloop(prs, prs->currcompiler->loop->start);
    prs->currcompiler->loop->start = increment_start;

    neon_astparser_emitpatchjump(prs, body_jump);

    prs->currcompiler->loop->body = prs->currcompiler->currfunc->blob->count;
    neon_astparser_parsestmt(prs);

    neon_astparser_emitloop(prs, prs->currcompiler->loop->start);

    neon_astparser_endloop(prs);
    neon_astparser_scopeend(prs);
}


/*
void neon_astparser_parsewhilestmt(NeonAstParser* p)
{
    int surroundingloopstart = p->innermostloopstart;
    int surroundingscopedepth = p->innermostloopscopedepth;
    // we'll be jumping back to right before the
    // expression after the loop body
    p->innermostloopstart = neon_astparser_currentblob(p)->count;
    neon_astparser_parseexpr(p);
    int exitjump = neon_astparser_emitjump(p, NEON_OP_JUMPIFFALSE);
    neon_astparser_emit1byte(p, NEON_OP_POPONE);
    neon_astparser_parsestmt(p);
    neon_astparser_emitloop(p, p->innermostloopstart);
    neon_astparser_emitpatchjump(p, exitjump);
    neon_astparser_emit1byte(p, NEON_OP_POPONE);
    neon_astparser_endloop(p);
    p->innermostloopstart = surroundingloopstart;
    p->innermostloopscopedepth = surroundingscopedepth;
}
*/

void neon_astparser_beginloop(NeonAstParser* prs, NeonAstLoop* loop)
{
    loop->start = neon_astparser_currentblob(prs)->count;
    loop->scopedepth = prs->currcompiler->scopedepth;
    loop->enclosing = prs->currcompiler->loop;
    prs->currcompiler->loop = loop;
}

void neon_astparser_endloop(NeonAstParser* prs)
{
    if(prs->currcompiler->loop->end != -1)
    {
        neon_astparser_emitpatchjump(prs, prs->currcompiler->loop->end);
        neon_astparser_emit1byte(prs, NEON_OP_POPONE);
    }
    int i = prs->currcompiler->loop->body;
    while(i < prs->currcompiler->currfunc->blob->count)
    {
        if(prs->currcompiler->currfunc->blob->bincode[i] == NEON_OP_PSEUDOBREAK)
        {
            prs->currcompiler->currfunc->blob->bincode[i] = NEON_OP_JUMPNOW;
            neon_astparser_emitpatchjump(prs, i + 1);
            i += 3;
        }
        else
        {
            //int neon_astparser_getcodeargscount(const int32_t *bytecode, const NeonValArray *constants, int ip);
            i += 1 + neon_astparser_getcodeargscount(prs->currcompiler->currfunc->blob->bincode, prs->currcompiler->currfunc->blob->constants, i);
        }
    }
    prs->currcompiler->loop = prs->currcompiler->loop->enclosing;
}

void neon_astparser_parsewhilestmt(NeonAstParser* prs)
{
    NeonAstLoop loop;
    neon_astparser_beginloop(prs, &loop);
    if(!neon_astparser_checkcurrent(prs, NEON_TOK_PARENOPEN))
    {
        neon_astparser_emit1byte(prs, NEON_OP_PUSHTRUE);
    }
    else
    { 
        neon_astparser_consume(prs, NEON_TOK_PARENOPEN, "Expect '(' after 'while'");
        neon_astparser_parseexpr(prs);
        neon_astparser_consume(prs, NEON_TOK_PARENCLOSE, "Expect ')' after condition");
    }

    /* Jump ot of the loop if the condition is false */
    prs->currcompiler->loop->end = neon_astparser_emitjump(prs, NEON_OP_JUMPIFFALSE);
    neon_astparser_emit1byte(prs, NEON_OP_POPONE);

    /* Compile the body */
    prs->currcompiler->loop->body = prs->currcompiler->currfunc->blob->count;
    neon_astparser_parsestmt(prs);

    /* Loop back to the start */
    neon_astparser_emitloop(prs, prs->currcompiler->loop->start);
    neon_astparser_endloop(prs);
}


/*
void neon_astparser_parsecontinuestmt(NeonAstParser* p)
{
    if(p->innermostloopstart == -1)
    {
        neon_astparser_raiseerror(p, "'continue' can only be used in a loop");
    }
    // discard local variables created in the loop
    neon_astparser_discardlocals(p, p->innermostloopscopedepth);
    // go back to the top of the loop
    neon_astparser_emitloop(p, p->innermostloopstart);
    neon_astparser_consumestmtend(p);
}
*/


static void neon_astparser_parsecontinuestmt(NeonAstParser* prs)
{
    if(prs->currcompiler->loop == NULL)
    {
        neon_astparser_raiseerror(prs, "Cannot use 'continue' outside of a loop");
    }
    /* Discard any locals created inside the loop */
    neon_astparser_discardlocals(prs, prs->currcompiler->loop->scopedepth + 1);
    /* Jump to the top of the innermost loop */
    neon_astparser_emitloop(prs, prs->currcompiler->loop->start);
    neon_astparser_consumestmtend(prs);
}

/*
void neon_astparser_parsebreakstmt(NeonAstParser* p)
{
    if(p->innermostloopstart == -1)
    {
        neon_astparser_raiseerror(p, "'break' can only be used in a loop");
    }
    // discard local variables created in the loop
    //  neon_astparser_discardlocals(p, p->innermostloopscopedepth);
    neon_astparser_emitjump(p, NEON_OP_PSEUDOBREAK);
    neon_astparser_consumestmtend(p);
}
*/

void neon_astparser_parsebreakstmt(NeonAstParser* prs)
{
    if(prs->currcompiler->loop == NULL)
    {
        neon_astparser_raiseerror(prs, "Cannot use 'break' outside of a loop");
    }
    /* Discard any locals created inside the loop */
    neon_astparser_discardlocals(prs, prs->currcompiler->loop->scopedepth + 1);
    neon_astparser_emitjump(prs, NEON_OP_PSEUDOBREAK);
    neon_astparser_consumestmtend(prs);
}

static void neon_astparser_parseifstmt(NeonAstParser* p)
{
    neon_astparser_parseexpr(p);
    int thenjump = neon_astparser_emitjump(p, NEON_OP_JUMPIFFALSE);
    neon_astparser_emit1byte(p, NEON_OP_POPONE);
    neon_astparser_parsestmt(p);
    int elsejump = neon_astparser_emitjump(p, NEON_OP_JUMPNOW);
    neon_astparser_emitpatchjump(p, thenjump);
    neon_astparser_emit1byte(p, NEON_OP_POPONE);
    if(neon_astparser_match(p, NEON_TOK_KWELSE))
    {
        neon_astparser_parsestmt(p);
    }
    neon_astparser_emitpatchjump(p, elsejump);
}

void neon_astparser_parsedebugprintstmt(NeonAstParser* prs)
{
    neon_astparser_parseexpr(prs);
    //neon_astparser_consume(prs, NEON_TOK_SEMICOLON, "expect ';' after value.");
    neon_astparser_skipsemicolon(prs);
    neon_astparser_emit1byte(prs, NEON_OP_DEBUGPRINT);
}

void neon_astparser_parsereturnstmt(NeonAstParser* prs)
{
    if(prs->currcompiler->type == NEON_TYPE_SCRIPT)
    {
        neon_astparser_raiseerror(prs, "cannot return from top-level code");
    }
    if(neon_astparser_match(prs, NEON_TOK_SEMICOLON) || neon_astparser_match(prs, NEON_TOK_NEWLINE))
    {
        neon_astparser_emitreturn(prs, false);
    }
    else
    {
        if(prs->currcompiler->type == NEON_TYPE_INITIALIZER)
        {
            neon_astparser_raiseerror(prs, "cannot return a value from an initializer");
        }
        neon_astparser_parseexpr(prs);
        //neon_astparser_consume(prs, NEON_TOK_SEMICOLON, "expect ';' after return value.");
        neon_astparser_skipsemicolon(prs);
        neon_astparser_emit1byte(prs, NEON_OP_RETURN);
    }
}

void neon_astparser_synchronize(NeonAstParser* prs)
{
    prs->panicmode = false;
    while(prs->current.type != NEON_TOK_EOF)
    {
        if(prs->previous.type == NEON_TOK_SEMICOLON)
        {
            return;
        }
        switch(prs->current.type)
        {
            case NEON_TOK_KWBREAK:
            case NEON_TOK_KWCONTINUE:
            case NEON_TOK_KWCLASS:
            case NEON_TOK_KWFUNCTION:
            case NEON_TOK_KWVAR:
            case NEON_TOK_KWFOR:
            case NEON_TOK_KWIF:
            case NEON_TOK_KWWHILE:
            case NEON_TOK_KWDEBUGPRINT:
            case NEON_TOK_KWGLOBAL:
            case NEON_TOK_KWRETURN:
                return;
            default:
                // Do nothing.
                break;
        }
        neon_astparser_advance(prs);
    }
}

void neon_astparser_parsedecl(NeonAstParser* prs)
{
    if(neon_astparser_match(prs, NEON_TOK_KWCLASS))
    {
        neon_astparser_parseclassdecl(prs);
    }
    else if(neon_astparser_match(prs, NEON_TOK_KWFUNCTION))
    {
        neon_astparser_parsefuncdecl(prs);
    }
    else if(neon_astparser_match(prs, NEON_TOK_KWVAR))
    {
        neon_astparser_parsevardecl(prs);
    }
    else
    {
        neon_astparser_parsestmt(prs);
    }
    /* Global Variables declaration < Global Variables match-var */
    // neon_astparser_parsestmt(prs);
    if(prs->panicmode)
    {
        neon_astparser_synchronize(prs);
    }
}

void neon_astparser_parsestmt(NeonAstParser* prs)
{
    if(neon_astparser_match(prs, NEON_TOK_KWGLOBAL))
    {
        neon_astparser_ruleglobalstmt(prs, false);
    }
    else if(neon_astparser_match(prs, NEON_TOK_KWDEBUGPRINT))
    {
        neon_astparser_parsedebugprintstmt(prs);
    }
    else if(neon_astparser_match(prs, NEON_TOK_KWBREAK))
    {
        neon_astparser_parsebreakstmt(prs);
    }
    else if(neon_astparser_match(prs, NEON_TOK_KWCONTINUE))
    {
        neon_astparser_parsecontinuestmt(prs);
    }
    else if(neon_astparser_match(prs, NEON_TOK_KWFOR))
    {
        neon_astparser_parseforstmt(prs);
    }
    else if(neon_astparser_match(prs, NEON_TOK_KWIF))
    {
        neon_astparser_parseifstmt(prs);
    }
    else if(neon_astparser_match(prs, NEON_TOK_KWRETURN))
    {
        neon_astparser_parsereturnstmt(prs);
    }
    else if(neon_astparser_match(prs, NEON_TOK_KWWHILE))
    {
        neon_astparser_parsewhilestmt(prs);
    }
    else if(neon_astparser_match(prs, NEON_TOK_BRACEOPEN))
    {
        neon_astparser_scopebegin(prs);
        neon_astparser_parseblock(prs);
        neon_astparser_scopeend(prs);
    }
    else
    {
        neon_astparser_parseexprstmt(prs, false, false);
    }
}



