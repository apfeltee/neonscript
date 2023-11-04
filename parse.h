
NeonAstScanner* neon_lex_make(NeonState* state, const char* source, size_t len)
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

void neon_lex_release(NeonAstScanner* scn)
{
    free(scn);
}

bool neon_lex_isalpha(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

bool neon_lex_isdigit(char c)
{
    return c >= '0' && c <= '9';
}

bool neon_lex_isatend(NeonAstScanner* scn)
{
    return *scn->current == '\0';
}

char neon_lex_advance(NeonAstScanner* scn)
{
    scn->current++;
    return scn->current[-1];
}

char neon_lex_peekcurrent(NeonAstScanner* scn)
{
    return *scn->current;
}

char neon_lex_peeknext(NeonAstScanner* scn)
{
    if(neon_lex_isatend(scn))
    {
        return '\0';
    }
    return scn->current[1];
}

bool neon_lex_match(NeonAstScanner* scn, char expected)
{
    if(neon_lex_isatend(scn))
        return false;
    if(*scn->current != expected)
        return false;
    scn->current++;
    return true;
}

NeonAstToken neon_lex_maketoken(NeonAstScanner* scn, NeonAstTokType type)
{
    NeonAstToken token;
    token.type = type;
    token.start = scn->start;
    token.length = (int)(scn->current - scn->start);
    token.line = scn->line;
    return token;
}

NeonAstToken neon_lex_makeerrortoken(NeonAstScanner* scn, const char* message)
{
    NeonAstToken token;
    token.type = NEON_TOK_ERROR;
    token.start = message;
    token.length = (int)strlen(message);
    token.line = scn->line;
    return token;
}

void neon_lex_skipspace(NeonAstScanner* scn)
{
    char c;
    for(;;)
    {
        c = neon_lex_peekcurrent(scn);
        switch(c)
        {
            case ' ':
            case '\r':
            case '\t':
                {
                    neon_lex_advance(scn);
                }
                break;
            case '\n':
                {
                    scn->line++;
                    neon_lex_advance(scn);
                }
                break;
            case '/':
                {
                    if(neon_lex_peeknext(scn) == '/')
                    {
                        // A comment goes until the end of the line.
                        while(neon_lex_peekcurrent(scn) != '\n' && !neon_lex_isatend(scn))
                            neon_lex_advance(scn);
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

NeonAstTokType neon_lex_scankeyword(NeonAstScanner* scn)
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

NeonAstToken neon_lex_scanident(NeonAstScanner* scn)
{
    while(neon_lex_isalpha(neon_lex_peekcurrent(scn)) || neon_lex_isdigit(neon_lex_peekcurrent(scn)))
    {
        neon_lex_advance(scn);
    }
    return neon_lex_maketoken(scn, neon_lex_scankeyword(scn));
}

NeonAstToken neon_lex_scannumber(NeonAstScanner* scn)
{
    while(neon_lex_isdigit(neon_lex_peekcurrent(scn)))
    {
        neon_lex_advance(scn);
    }
    // Look for a fractional part.
    if(neon_lex_peekcurrent(scn) == '.' && neon_lex_isdigit(neon_lex_peeknext(scn)))
    {
        // Consume the ".".
        neon_lex_advance(scn);
        while(neon_lex_isdigit(neon_lex_peekcurrent(scn)))
        {
            neon_lex_advance(scn);
        }
    }
    return neon_lex_maketoken(scn, NEON_TOK_NUMBER);
}

NeonAstToken neon_lex_scanstring(NeonAstScanner* scn, char quote)
{
    /*
    char peek;
    while(true)
    {
        peek = neon_lex_peekcurrent(scn);
        if((peek == '"') || !neon_lex_isatend(scn))
        {
            break;
        }
        if(neon_lex_peekcurrent(scn) == '\n')
        {
            scn->line++;
        }
        neon_lex_advance(scn);
    }
    if(neon_lex_isatend(scn))
    {
        return neon_lex_makeerrortoken(scn, "unterminated string");
    }
    // The closing quote.
    neon_lex_advance(scn);
    return neon_lex_maketoken(scn, NEON_TOK_STRING);
    */




    while(neon_lex_peekcurrent(scn) != quote && !neon_lex_isatend(scn))
    {
        if(neon_lex_peekcurrent(scn) == '\\' && (neon_lex_peeknext(scn) == quote || neon_lex_peeknext(scn) == '\\'))
        {
            neon_lex_advance(scn);
        }
        neon_lex_advance(scn);
    }
    if(neon_lex_isatend(scn))
    {
        return neon_lex_makeerrortoken(scn, "unterminated string");
    }
    neon_lex_match(scn, quote);// the closing quote
    return neon_lex_maketoken(scn, NEON_TOK_STRING);
}

NeonAstToken neon_lex_scantoken(NeonAstScanner* scn)
{
    char c;
    neon_lex_skipspace(scn);
    scn->start = scn->current;
    if(neon_lex_isatend(scn))
    {
        return neon_lex_maketoken(scn, NEON_TOK_EOF);
    }
    c = neon_lex_advance(scn);
    if(neon_lex_isalpha(c))
    {
        return neon_lex_scanident(scn);
    }
    if(neon_lex_isdigit(c))
    {
        return neon_lex_scannumber(scn);
    }
    switch(c)
    {
        case '\n':
            {
                return neon_lex_maketoken(scn, NEON_TOK_NEWLINE);
            }
            break;
        case '(':
            {
                return neon_lex_maketoken(scn, NEON_TOK_PARENOPEN);
            }
            break;
        case ')':
            {
                return neon_lex_maketoken(scn, NEON_TOK_PARENCLOSE);
            }
            break;
        case '{':
            {
                return neon_lex_maketoken(scn, NEON_TOK_BRACEOPEN);
            }
            break;
        case '}':
            {
                return neon_lex_maketoken(scn, NEON_TOK_BRACECLOSE);
            }
            break;
        case '[':
            {
                return neon_lex_maketoken(scn, NEON_TOK_BRACKETOPEN);
            }
            break;
        case ']':
            {
                return neon_lex_maketoken(scn, NEON_TOK_BRACKETCLOSE);
            }
            break;
        case ';':
            {
                return neon_lex_maketoken(scn, NEON_TOK_SEMICOLON);
            }
            break;
        case ',':
            {
                return neon_lex_maketoken(scn, NEON_TOK_COMMA);
            }
            break;
        case '.':
            {
                return neon_lex_maketoken(scn, NEON_TOK_DOT);
            }
            break;
        case '-':
            {
                if(neon_lex_match(scn, '-'))
                {
                    return neon_lex_maketoken(scn, NEON_TOK_DECREMENT);
                }
                else if(neon_lex_match(scn, '='))
                {
                    return neon_lex_maketoken(scn, NEON_TOK_ASSIGNMINUS);
                }
                return neon_lex_maketoken(scn, NEON_TOK_MINUS);
            }
            break;
        case '+':
            {
                if(neon_lex_match(scn, '+'))
                {
                    return neon_lex_maketoken(scn, NEON_TOK_INCREMENT);
                }
                else if(neon_lex_match(scn, '='))
                {
                    return neon_lex_maketoken(scn, NEON_TOK_ASSIGNPLUS);
                }
                return neon_lex_maketoken(scn, NEON_TOK_PLUS);
            }
            break;
        case '&':
            {
                if(neon_lex_match(scn, '&'))
                {
                    return neon_lex_maketoken(scn, NEON_TOK_KWAND);
                }
                return neon_lex_maketoken(scn, NEON_TOK_BINAND);
            }
            break;
        case '|':
            {
                if(neon_lex_match(scn, '|'))
                {
                    return neon_lex_maketoken(scn, NEON_TOK_KWOR);
                }
                return neon_lex_maketoken(scn, NEON_TOK_BINOR);
            }
            break;
        case '^':
            {
                return neon_lex_maketoken(scn, NEON_TOK_BINXOR);
            }
            break;
        case '%':
            {
                if(neon_lex_match(scn, '='))
                {
                    return neon_lex_maketoken(scn, NEON_TOK_ASSIGNMODULO);
                }
                return neon_lex_maketoken(scn, NEON_TOK_MODULO);
            }
            break;
        case '/':
            {
                if(neon_lex_match(scn, '='))
                {
                    return neon_lex_maketoken(scn, NEON_TOK_ASSIGNDIV);
                }
                return neon_lex_maketoken(scn, NEON_TOK_SLASH);
            }
            break;
        case '*':
            {
                if(neon_lex_match(scn, '='))
                {
                    return neon_lex_maketoken(scn, NEON_TOK_ASSIGNMULT);
                }
                return neon_lex_maketoken(scn, NEON_TOK_STAR);
            }
            break;
        case '!':
            {
                if(neon_lex_match(scn, '='))
                {
                    return neon_lex_maketoken(scn, NEON_TOK_COMPNOTEQUAL);
                }
                return neon_lex_maketoken(scn, NEON_TOK_EXCLAM);
            }
            break;
        case '=':
            {
                if(neon_lex_match(scn, '='))
                {
                    return neon_lex_maketoken(scn, NEON_TOK_COMPEQUAL);
                }
                return neon_lex_maketoken(scn, NEON_TOK_ASSIGN);
            }
            break;
        case '<':
            {
                if(neon_lex_match(scn, '='))
                {
                    return neon_lex_maketoken(scn, NEON_TOK_COMPLESSEQUAL);
                }
                else if(neon_lex_match(scn, '<'))
                {
                    return neon_lex_maketoken(scn, NEON_TOK_SHIFTLEFT);
                }
                return neon_lex_maketoken(scn, NEON_TOK_COMPLESSTHAN);
            }
            break;
        case '>':
            {
                if(neon_lex_match(scn, '='))
                {
                    return neon_lex_maketoken(scn, NEON_TOK_COMPGREATEREQUAL);
                }
                else if(neon_lex_match(scn, '>'))
                {
                    return neon_lex_maketoken(scn, NEON_TOK_SHIFTRIGHT);
                }
                return neon_lex_maketoken(scn, NEON_TOK_COMPGREATERTHAN);
            }
            break;
        case '"':
            {
                return neon_lex_scanstring(scn, '"');
            }
            break;
    }
    return neon_lex_makeerrortoken(scn, "unexpected character");
}

NeonAstParser* neon_prs_make(NeonState* state)
{
    NeonAstParser* prs;
    prs = (NeonAstParser*)malloc(sizeof(NeonAstParser));
    prs->pvm = state;
    prs->currcompiler = NULL;
    prs->currclass = NULL;
    prs->haderror = false;
    prs->panicmode = false;
    return prs;
}

void neon_prs_release(NeonAstParser* prs)
{
    if(prs->pscn != NULL)
    {
        neon_lex_release(prs->pscn);
    }
    free(prs);
}

NeonChunk* neon_prs_currentchunk(NeonAstParser* prs)
{
    return prs->currcompiler->compiledfn->chunk;
}

void neon_prs_vraiseattoken(NeonAstParser* prs, NeonAstToken* token, const char* message, va_list va)
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

void neon_prs_raiseerror(NeonAstParser* prs, const char* message, ...)
{
    va_list va;
    va_start(va, message);
    neon_prs_vraiseattoken(prs, &prs->previous, message, va);
    va_end(va);
}

void neon_prs_raiseatcurrent(NeonAstParser* prs, const char* message, ...)
{
    va_list va;
    va_start(va, message);
    neon_prs_vraiseattoken(prs, &prs->current, message, va);
    va_end(va);
}

const char* neon_prs_op2str(int32_t opcode)
{
    switch(opcode)
    {
        case NEON_OP_PUSHCONST: return "NEON_OP_PUSHCONST";
        case NEON_OP_PUSHNIL: return "NEON_OP_PUSHNIL";
        case NEON_OP_PUSHTRUE: return "NEON_OP_PUSHTRUE";
        case NEON_OP_PUSHFALSE: return "NEON_OP_PUSHFALSE";
        case NEON_OP_PUSHONE: return "NEON_OP_PUSHONE";
        case NEON_OP_POP: return "NEON_OP_POP";
        case NEON_OP_DUP: return "NEON_OP_DUP";
        case NEON_OP_LOCALGET: return "NEON_OP_LOCALGET";
        case NEON_OP_LOCALSET: return "NEON_OP_LOCALSET";
        case NEON_OP_GLOBALGET: return "NEON_OP_GLOBALGET";
        case NEON_OP_GLOBALDEFINE: return "NEON_OP_GLOBALDEFINE";
        case NEON_OP_GLOBALSET: return "NEON_OP_GLOBALSET";
        case NEON_OP_UPVALGET: return "NEON_OP_UPVALGET";
        case NEON_OP_UPVALSET: return "NEON_OP_UPVALSET";
        case NEON_OP_PROPERTYGET: return "NEON_OP_PROPERTYGET";
        case NEON_OP_PROPERTYSET: return "NEON_OP_PROPERTYSET";
        case NEON_OP_INSTGETSUPER: return "NEON_OP_INSTGETSUPER";
        case NEON_OP_EQUAL: return "NEON_OP_EQUAL";
        case NEON_OP_PRIMGREATER: return "NEON_OP_PRIMGREATER";
        case NEON_OP_PRIMLESS: return "NEON_OP_PRIMLESS";
        case NEON_OP_PRIMADD: return "NEON_OP_PRIMADD";
        case NEON_OP_PRIMSUBTRACT: return "NEON_OP_PRIMSUBTRACT";
        case NEON_OP_PRIMMULTIPLY: return "NEON_OP_PRIMMULTIPLY";
        case NEON_OP_PRIMDIVIDE: return "NEON_OP_PRIMDIVIDE";
        case NEON_OP_PRIMNOT: return "NEON_OP_PRIMNOT";
        case NEON_OP_PRIMNEGATE: return "NEON_OP_PRIMNEGATE";
        case NEON_OP_DEBUGPRINT: return "NEON_OP_DEBUGPRINT";
        case NEON_OP_GLOBALSTMT: return "NEON_OP_GLOBALSTMT";
        case NEON_OP_JUMPNOW: return "NEON_OP_JUMPNOW";
        case NEON_OP_JUMPIFFALSE: return "NEON_OP_JUMPIFFALSE";
        case NEON_OP_LOOP: return "NEON_OP_LOOP";
        case NEON_OP_CALL: return "NEON_OP_CALL";
        case NEON_OP_INSTTHISINVOKE: return "NEON_OP_INSTTHISINVOKE";
        case NEON_OP_INSTSUPERINVOKE: return "NEON_OP_INSTSUPERINVOKE";
        case NEON_OP_CLOSURE: return "NEON_OP_CLOSURE";
        case NEON_OP_UPVALCLOSE: return "NEON_OP_UPVALCLOSE";
        case NEON_OP_RETURN: return "NEON_OP_RETURN";
        case NEON_OP_CLASS: return "NEON_OP_CLASS";
        case NEON_OP_INHERIT: return "NEON_OP_INHERIT";
        case NEON_OP_METHOD: return "NEON_OP_METHOD";
        case NEON_OP_PSEUDOBREAK: return "NEON_OP_PSEUDOBREAK";

    }
    return "?unknown?";
}

void neon_prs_skipsemicolon(NeonAstParser* prs)
{
    while(neon_prs_match(prs, NEON_TOK_NEWLINE))
    {
    }
    while(neon_prs_match(prs, NEON_TOK_SEMICOLON))
    {
    }
}

void neon_prs_advance(NeonAstParser* prs)
{
    prs->previous = prs->current;
    for(;;)
    {
        prs->current = neon_lex_scantoken(prs->pscn);
        if(prs->current.type != NEON_TOK_ERROR)
        {
            break;
        }
        neon_prs_raiseatcurrent(prs, prs->current.start);
    }
}

void neon_prs_consume(NeonAstParser* prs, NeonAstTokType type, const char* message)
{
    if(prs->current.type == type)
    {
        neon_prs_advance(prs);
        return;
    }
    neon_prs_raiseatcurrent(prs, message);
}

bool neon_prs_checkcurrent(NeonAstParser* prs, NeonAstTokType type)
{
    return prs->current.type == type;
}

bool neon_prs_match(NeonAstParser* prs, NeonAstTokType type)
{
    if(!neon_prs_checkcurrent(prs, type))
    {
        return false;
    }
    neon_prs_advance(prs);
    return true;
}

void neon_prs_emit1byte(NeonAstParser* prs, int32_t byte)
{
    neon_chunk_pushbyte(prs->pvm, neon_prs_currentchunk(prs), byte, prs->previous.line);
}

void neon_prs_emit2byte(NeonAstParser* prs, int32_t byte1, int32_t byte2)
{
    neon_prs_emit1byte(prs, byte1);
    neon_prs_emit1byte(prs, byte2);
}

void neon_prs_emitloop(NeonAstParser* prs, int loopstart)
{
    int offset;
    neon_prs_emit1byte(prs, NEON_OP_LOOP);
    offset = neon_prs_currentchunk(prs)->count - loopstart + 2;
    if(offset > UINT16_MAX)
    {
        neon_prs_raiseerror(prs, "loop body too large");
    }
    neon_prs_emit1byte(prs, (offset >> 8) & 0xff);
    neon_prs_emit1byte(prs, offset & 0xff);
}


int neon_prs_realgetcodeargscount(const int32_t* code, int ip)
{
    int32_t op = code[ip];
    if(op == -1)
    {
        return 0;
    }
    switch(op)
    {
        case NEON_OP_PUSHTRUE:
        case NEON_OP_PUSHFALSE:
        case NEON_OP_PUSHNIL:
        case NEON_OP_POP:
        case NEON_OP_EQUAL:
        case NEON_OP_PRIMGREATER:
        case NEON_OP_PRIMLESS:
        case NEON_OP_PRIMADD:
        case NEON_OP_PRIMSUBTRACT:
        case NEON_OP_PRIMMULTIPLY:
        case NEON_OP_PRIMDIVIDE:
        case NEON_OP_PRIMBINAND:
        case NEON_OP_PRIMBINOR:
        case NEON_OP_PRIMBINXOR:
        case NEON_OP_PRIMMODULO:
        case NEON_OP_PRIMSHIFTLEFT:
        case NEON_OP_PRIMSHIFTRIGHT:
        case NEON_OP_PRIMNOT:
        case NEON_OP_PRIMNEGATE:
        case NEON_OP_DEBUGPRINT:
        case NEON_OP_GLOBALSTMT:
        case NEON_OP_DUP:
        case NEON_OP_PUSHONE:
        case NEON_OP_UPVALCLOSE:
        case NEON_OP_RETURN:
        case NEON_OP_INHERIT:
        case NEON_OP_INDEXSET:
            return 0;
        case NEON_OP_POPN:
        case NEON_OP_PUSHCONST:
        //case NEON_OP_CONSTANT_LONG:
        //case NEON_OP_POPN:
        case NEON_OP_LOCALSET:
        case NEON_OP_LOCALGET:
        case NEON_OP_UPVALSET:
        case NEON_OP_UPVALGET:
        case NEON_OP_PROPERTYGET:
        case NEON_OP_PROPERTYSET:
        /*
        case NEON_OP_GET_EXPR_PROPERTY:
        case NEON_OP_SET_EXPR_PROPERTY:
        */
        case NEON_OP_CALL:
        case NEON_OP_INSTGETSUPER:
        case NEON_OP_GLOBALGET:
        case NEON_OP_GLOBALDEFINE:
        case NEON_OP_GLOBALSET:
        case NEON_OP_CLOSURE:
        case NEON_OP_CLASS:
        case NEON_OP_METHOD:
        case NEON_OP_INDEXGET:
            return 1;
        case NEON_OP_MAKEARRAY:
        case NEON_OP_MAKEMAP:
        case NEON_OP_JUMPNOW:
        case NEON_OP_JUMPIFFALSE:
        case NEON_OP_PSEUDOBREAK:
        case NEON_OP_LOOP:
        case NEON_OP_INSTTHISINVOKE:
        case NEON_OP_INSTSUPERINVOKE:
            return 2;
    }
    fprintf(stderr, "internal error: failed to compute operand argument size of %d (%s)\n", op, neon_prs_op2str(op));
    return -1;
}

int neon_prs_getcodeargscount(const int32_t* bytecode, int ip)
{
    int rc;
    //const char* os;
    rc = neon_prs_realgetcodeargscount(bytecode, ip);
    //os = neon_prs_op2str(bytecode[ip]);
    //fprintf(stderr, "getcodeargscount(..., code=%s) = %d\n", os, rc);
    return rc;
}

void neon_prs_startloop(NeonAstParser* prs, NeonAstLoop* loop)
{
    loop->enclosing = prs->currcompiler->loop;
    loop->start = neon_prs_currentchunk(prs)->count;
    loop->scopedepth = prs->currcompiler->scopedepth;
    prs->currcompiler->loop = loop;
}

void neon_prs_endloop(NeonAstParser* prs)
{
    int i;
    NeonChunk* chunk;
    i = prs->currcompiler->loop->body;
    chunk = neon_prs_currentchunk(prs);
    while(i < chunk->count)
    {
        if(chunk->bincode[i] == NEON_OP_PSEUDOBREAK)
        {
            chunk->bincode[i] = NEON_OP_JUMPNOW;
            neon_prs_emitpatchjump(prs, i + 1);
            i += 3;
        }
        else
        {
            i += 1 + neon_prs_getcodeargscount(chunk->bincode, i);
        }
    }
    prs->currcompiler->loop = prs->currcompiler->loop->enclosing;
}

int neon_prs_emitjump(NeonAstParser* prs, int32_t instruction)
{
    neon_prs_emit1byte(prs, instruction);
    neon_prs_emit1byte(prs, 0xff);
    neon_prs_emit1byte(prs, 0xff);
    return neon_prs_currentchunk(prs)->count - 2;
}

void neon_prs_emitreturn(NeonAstParser* prs, bool fromtoplevel)
{
    if(prs->currcompiler->type == NEON_TYPE_INITIALIZER)
    {
        neon_prs_emit2byte(prs, NEON_OP_LOCALGET, 0);
    }
    else
    {
        //neon_prs_emit1byte(prs, NEON_OP_PUSHNIL);
    }
    if(fromtoplevel && prs->iseval)
    {
        neon_prs_emit1byte(prs, NEON_OP_RESTOREFRAME);
    }
    neon_prs_emit1byte(prs, NEON_OP_RETURN);
}

int32_t neon_prs_makeconstant(NeonAstParser* prs, NeonValue value)
{
    int constant;
    constant = neon_chunk_pushconst(prs->pvm, neon_prs_currentchunk(prs), value);
    if(constant > UINT8_MAX)
    {
        neon_prs_raiseerror(prs, "too many constants in one chunk");
        return 0;
    }
    return (int32_t)constant;
}

void neon_prs_emitconstant(NeonAstParser* prs, NeonValue value)
{
    neon_prs_emit2byte(prs, NEON_OP_PUSHCONST, neon_prs_makeconstant(prs, value));
}

void neon_prs_emitpatchjump(NeonAstParser* prs, int offset)
{
    int jump;
    // -2 to adjust for the bytecode for the jump offset itself.
    jump = neon_prs_currentchunk(prs)->count - offset - 2;
    if(jump > UINT16_MAX)
    {
        neon_prs_raiseerror(prs, "too much code to jump over");
    }
    neon_prs_currentchunk(prs)->bincode[offset] = (jump >> 8) & 0xff;
    neon_prs_currentchunk(prs)->bincode[offset + 1] = jump & 0xff;
}

void neon_prs_compilerinit(NeonAstParser* prs, NeonAstCompiler* compiler, NeonAstFuncType type)
{
    NeonAstLocal* local;
    compiler->enclosing = prs->currcompiler;
    compiler->compiledfn = NULL;
    compiler->type = type;
    compiler->localcount = 0;
    compiler->scopedepth = 0;
    compiler->compiledfn = neon_object_makefunction(prs->pvm);
    prs->currcompiler = compiler;
    if(type != NEON_TYPE_SCRIPT)
    {
        prs->currcompiler->compiledfn->name = neon_string_copy(prs->pvm, prs->previous.start, prs->previous.length);
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

NeonObjScriptFunction* neon_prs_compilerfinish(NeonAstParser* prs, bool ismainfn)
{
    NeonObjScriptFunction* fn;
    neon_prs_emitreturn(prs, true);
    if(ismainfn)
    {
        //neon_prs_emit1byte(prs, NEON_OP_HALTVM);
    }
    fn = prs->currcompiler->compiledfn;
#if (DEBUG_PRINT_CODE == 1)
    if(!prs->haderror)
    {
        neon_chunk_disasm(prs->pvm, prs->pvm->stderrwriter, neon_prs_currentchunk(prs), fn->name != NULL ? fn->name->sbuf->data : "<script>");
    }
#endif
    prs->currcompiler = prs->currcompiler->enclosing;
    return fn;
}

void neon_prs_scopebegin(NeonAstParser* prs)
{
    prs->currcompiler->scopedepth++;
}

void neon_prs_scopeend(NeonAstParser* prs)
{
    NeonAstCompiler* pc;
    pc = prs->currcompiler;
    pc->scopedepth--;
    while(pc->localcount > 0 && pc->locals[pc->localcount - 1].depth > pc->scopedepth)
    {
        if(pc->locals[pc->localcount - 1].iscaptured)
        {
            neon_prs_emit1byte(prs, NEON_OP_UPVALCLOSE);
        }
        else
        {
            neon_prs_emit1byte(prs, NEON_OP_POP);
        }
        pc->localcount--;
    }
}

int32_t neon_prs_makeidentconstant(NeonAstParser* prs, NeonAstToken* name)
{
    return neon_prs_makeconstant(prs, neon_value_fromobject(neon_string_copy(prs->pvm, name->start, name->length)));
}

bool neon_prs_identsequal(NeonAstToken* a, NeonAstToken* b)
{
    if(a->length != b->length)
    {
        return false;
    }
    return memcmp(a->start, b->start, a->length) == 0;
}


void neon_prs_addlocal(NeonAstParser* prs, NeonAstToken name)
{
    NeonAstLocal* local;
    if(prs->currcompiler->localcount == NEON_MAX_COMPLOCALS)
    {
        neon_prs_raiseerror(prs, "too many local variables in function");
        return;
    }
    local = &prs->currcompiler->locals[prs->currcompiler->localcount++];
    local->name = name;
    local->depth = -1;
    local->iscaptured = false;
}

int neon_prs_discardlocals(NeonAstParser* prs, NeonAstCompiler* current)
{
    int n;
    int lc;
    int depth;
    depth = current->loop->scopedepth + 1;
    lc = current->localcount - 1;
    n = 0;
    while(lc >= 0 && current->locals[lc].depth >= depth)
    {
        current->localcount--;
        n++;
        lc--;
    }
    if(n != 0)
    {
        neon_prs_emit2byte(prs, NEON_OP_POPN, (int32_t)n);
    }
    return current->localcount - lc - 1;
}

void neon_prs_parsevarident(NeonAstParser* prs)
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
        if(neon_prs_identsequal(name, &local->name))
        {
            neon_prs_raiseerror(prs, "already a variable with this name in this scope");
        }
    }
    neon_prs_addlocal(prs, *name);
}

int32_t neon_prs_parsevarname(NeonAstParser* prs, const char* errormessage)
{
    neon_prs_consume(prs, NEON_TOK_IDENTIFIER, errormessage);
    neon_prs_parsevarident(prs);
    if(prs->currcompiler->scopedepth > 0)
    {
        return 0;
    }
    return neon_prs_makeidentconstant(prs, &prs->previous);
}

void neon_prs_markinit(NeonAstParser* prs)
{
    if(prs->currcompiler->scopedepth == 0)
    {
        return;
    }
    prs->currcompiler->locals[prs->currcompiler->localcount - 1].depth = prs->currcompiler->scopedepth;
}

void neon_prs_emitdefvar(NeonAstParser* prs, int32_t global)
{
    if(prs->currcompiler->scopedepth > 0)
    {
        neon_prs_markinit(prs);
        return;
    }
    neon_prs_emit2byte(prs, NEON_OP_GLOBALDEFINE, global);
}

int32_t neon_prs_parsearglist(NeonAstParser* prs)
{
    int32_t argc;
    argc = 0;
    if(!neon_prs_checkcurrent(prs, NEON_TOK_PARENCLOSE))
    {
        do
        {
            neon_prs_parseexpr(prs);
            if(argc == 255)
            {
                neon_prs_raiseerror(prs, "cannot have more than 255 arguments");
            }
            argc++;
        } while(neon_prs_match(prs, NEON_TOK_COMMA));
    }
    neon_prs_consume(prs, NEON_TOK_PARENCLOSE, "expected ')' after arguments");
    return argc;
}

void neon_prs_ruleand(NeonAstParser* prs, bool canassign)
{
    (void)canassign;
    int endjump;
    endjump = neon_prs_emitjump(prs, NEON_OP_JUMPIFFALSE);
    neon_prs_emit1byte(prs, NEON_OP_POP);
    neon_prs_parseprec(prs, NEON_PREC_AND);
    neon_prs_emitpatchjump(prs, endjump);
}

void neon_prs_rulebinary(NeonAstParser* prs, bool canassign)
{
    NeonAstRule* rule;
    NeonAstTokType ot;
    (void)canassign;
    ot = prs->previous.type;
    rule = neon_prs_getrule(ot);
    neon_prs_parseprec(prs, (NeonAstPrecedence)(rule->precedence + 1));
    switch(ot)
    {
        case NEON_TOK_COMPNOTEQUAL:
            neon_prs_emit2byte(prs, NEON_OP_EQUAL, NEON_OP_PRIMNOT);
            break;
        case NEON_TOK_COMPEQUAL:
            neon_prs_emit1byte(prs, NEON_OP_EQUAL);
            break;
        case NEON_TOK_COMPGREATERTHAN:
            neon_prs_emit1byte(prs, NEON_OP_PRIMGREATER);
            break;
        case NEON_TOK_COMPGREATEREQUAL:
            neon_prs_emit2byte(prs, NEON_OP_PRIMLESS, NEON_OP_PRIMNOT);
            break;
        case NEON_TOK_COMPLESSTHAN:
            neon_prs_emit1byte(prs, NEON_OP_PRIMLESS);
            break;
        case NEON_TOK_COMPLESSEQUAL:
            neon_prs_emit2byte(prs, NEON_OP_PRIMGREATER, NEON_OP_PRIMNOT);
            break;
        case NEON_TOK_PLUS:
            neon_prs_emit1byte(prs, NEON_OP_PRIMADD);
            break;
        case NEON_TOK_MINUS:
            neon_prs_emit1byte(prs, NEON_OP_PRIMSUBTRACT);
            break;
        case NEON_TOK_STAR:
            neon_prs_emit1byte(prs, NEON_OP_PRIMMULTIPLY);
            break;
        case NEON_TOK_SLASH:
            neon_prs_emit1byte(prs, NEON_OP_PRIMDIVIDE);
            break;
        case NEON_TOK_MODULO:
            neon_prs_emit1byte(prs, NEON_OP_PRIMMODULO);
            break;
        case NEON_TOK_BINAND:
            neon_prs_emit1byte(prs, NEON_OP_PRIMBINAND);
            break;
        case NEON_TOK_BINOR:
            neon_prs_emit1byte(prs, NEON_OP_PRIMBINOR);
            break;
        case NEON_TOK_BINXOR:
            neon_prs_emit1byte(prs, NEON_OP_PRIMBINXOR);
            break;
        case NEON_TOK_SHIFTLEFT:
            neon_prs_emit1byte(prs, NEON_OP_PRIMSHIFTLEFT);
            break;
        case NEON_TOK_SHIFTRIGHT:
            neon_prs_emit1byte(prs, NEON_OP_PRIMSHIFTRIGHT);
            break;
        default:
            return;// Unreachable.
    }
}

void neon_prs_rulecall(NeonAstParser* prs, bool canassign)
{
    int32_t argc;
    (void)canassign;
    argc = neon_prs_parsearglist(prs);
    neon_prs_emit2byte(prs, NEON_OP_CALL, argc);
}

void neon_prs_ruledot(NeonAstParser* prs, bool canassign)
{
    int32_t name;
    int32_t argc;
    (void)canassign;
    neon_prs_consume(prs, NEON_TOK_IDENTIFIER, "expect property name after '.'.");
    name = neon_prs_makeidentconstant(prs, &prs->previous);
    if(canassign && neon_prs_match(prs, NEON_TOK_ASSIGN))
    {
        neon_prs_parseexpr(prs);
        neon_prs_emit2byte(prs, NEON_OP_PROPERTYSET, name);
    }
    else if(neon_prs_match(prs, NEON_TOK_PARENOPEN))
    {
        argc = neon_prs_parsearglist(prs);
        neon_prs_emit2byte(prs, NEON_OP_INSTTHISINVOKE, name);
        neon_prs_emit1byte(prs, argc);
    }
    else
    {
        neon_prs_emit2byte(prs, NEON_OP_PROPERTYGET, name);
    }
}

void neon_prs_ruleliteral(NeonAstParser* prs, bool canassign)
{
    (void)canassign;
    switch(prs->previous.type)
    {
        case NEON_TOK_KWFALSE:
            neon_prs_emit1byte(prs, NEON_OP_PUSHFALSE);
            break;
        case NEON_TOK_KWNIL:
            neon_prs_emit1byte(prs, NEON_OP_PUSHNIL);
            break;
        case NEON_TOK_KWTRUE:
            neon_prs_emit1byte(prs, NEON_OP_PUSHTRUE);
            break;
        default:
            return;// Unreachable.
    }
}

void neon_prs_rulegrouping(NeonAstParser* prs, bool canassign)
{
    (void)canassign;
    neon_prs_parseexpr(prs);
    neon_prs_consume(prs, NEON_TOK_PARENCLOSE, "expect ')' after expression");
}

void neon_prs_rulenumber(NeonAstParser* prs, bool canassign)
{
    double value;
    (void)canassign;
    value = strtod(prs->previous.start, NULL);
    neon_prs_emitconstant(prs, neon_value_makenumber(value));
}

void neon_prs_ruleor(NeonAstParser* prs, bool canassign)
{
    int endjump;
    int elsejump;
    (void)canassign;
    elsejump = neon_prs_emitjump(prs, NEON_OP_JUMPIFFALSE);
    endjump = neon_prs_emitjump(prs, NEON_OP_JUMPNOW);
    neon_prs_emitpatchjump(prs, elsejump);
    neon_prs_emit1byte(prs, NEON_OP_POP);
    neon_prs_parseprec(prs, NEON_PREC_OR);
    neon_prs_emitpatchjump(prs, endjump);
}

#define stringesc1(c, rpl1) \
    case c: \
        { \
            buf[pi] = rpl1; \
            pi += 1; \
            i += 1; \
        } \
        break;

void neon_prs_rulestring(NeonAstParser* prs, bool canassign)
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
                if(nextc == '\\')
                {
                    buf[pi+0] = '\\';
                    buf[pi+1] = '\\';
                    pi += 2;
                }
                /*
                else if(nextc == '"')
                {
                    buf[pi+0] = '"';
                    pi += 2;
                }
                */
                else
                {

                    switch(nextc)
                    {
                        stringesc1('0', '\0');
                        stringesc1('1', '\1');
                        stringesc1('2', '\2');
                        stringesc1('n', '\n');
                        stringesc1('t', '\t');
                        stringesc1('r', '\r');
                        stringesc1('e', '\e');
                        stringesc1('"', '"');
                        default:
                            {
                                neon_prs_raiseerror(prs, "unknown string escape character '%c' (%d)", nextc, nextc);
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
    neon_prs_emitconstant(prs, neon_value_fromobject(os));

}

int neon_prs_resolvelocal(NeonAstParser* prs, NeonAstCompiler* compiler, NeonAstToken* name)
{
    int i;
    NeonAstLocal* local;
    for(i = compiler->localcount - 1; i >= 0; i--)
    {
        local = &compiler->locals[i];
        if(neon_prs_identsequal(name, &local->name))
        {
            if(local->depth == -1)
            {
                neon_prs_raiseerror(prs, "cannot read local variable in its own initializer");
            }
            return i;
        }
    }
    return -1;
}

int neon_prs_addupval(NeonAstParser* prs, NeonAstCompiler* compiler, int32_t index, bool islocal)
{
    int i;
    int upvaluecount;
    NeonAstUpvalue* upvalue;
    upvaluecount = compiler->compiledfn->upvaluecount;
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
        neon_prs_raiseerror(prs, "too many closure variables in function");
        return 0;
    }
    compiler->compupvals[upvaluecount].islocal = islocal;
    compiler->compupvals[upvaluecount].index = index;
    return compiler->compiledfn->upvaluecount++;
}


int neon_prs_resolveupval(NeonAstParser* prs, NeonAstCompiler* compiler, NeonAstToken* name)
{
    int localidx;
    int upvalue;
    if(compiler->enclosing == NULL)
    {
        return -1;
    }
    localidx = neon_prs_resolvelocal(prs, compiler->enclosing, name);
    if(localidx != -1)
    {
        compiler->enclosing->locals[localidx].iscaptured = true;
        return neon_prs_addupval(prs, compiler, (int32_t)localidx, true);
    }
    upvalue = neon_prs_resolveupval(prs, compiler->enclosing, name);
    if(upvalue != -1)
    {
        return neon_prs_addupval(prs, compiler, (int32_t)upvalue, false);
    }
    return -1;
}

void neon_prs_parsenamedvar(NeonAstParser* prs, NeonAstToken name, bool canassign)
{
    int32_t getop;
    int32_t setop;
    int arg;
    (void)canassign;
    arg = neon_prs_resolvelocal(prs, prs->currcompiler, &name);
    if(arg != -1)
    {
        getop = NEON_OP_LOCALGET;
        setop = NEON_OP_LOCALSET;
        return neon_prs_doassign(prs, getop, setop, arg, canassign);
    }
    else
    {
        arg = neon_prs_resolveupval(prs, prs->currcompiler, &name);
        if(arg != -1)
        {
            getop = NEON_OP_UPVALGET;
            setop = NEON_OP_UPVALSET;
            return neon_prs_doassign(prs, getop, setop, arg, canassign);
        }
        else
        {
            arg = neon_prs_makeidentconstant(prs, &name);
            getop = NEON_OP_GLOBALGET;
            setop = NEON_OP_GLOBALSET;
            return neon_prs_doassign(prs, getop, setop, arg, canassign);
        }
    }
}

void neon_prs_rulevariable(NeonAstParser* prs, bool canassign)
{
    neon_prs_parsenamedvar(prs, prs->previous, canassign);
}

NeonAstToken neon_prs_makesyntoken(NeonAstParser* prs, const char* text)
{
    NeonAstToken token;
    (void)prs;
    token.start = text;
    token.length = (int)strlen(text);
    return token;
}

void neon_prs_rulesuper(NeonAstParser* prs, bool canassign)
{
    int32_t name;
    int32_t argc;
    (void)canassign;
    if(prs->currclass == NULL)
    {
        neon_prs_raiseerror(prs, "cannot use 'super' outside of a class");
    }
    else if(!prs->currclass->hassuperclass)
    {
        neon_prs_raiseerror(prs, "cannot use 'super' in a class with no superclass");
    }
    neon_prs_consume(prs, NEON_TOK_DOT, "expected '.' after 'super'");
    neon_prs_consume(prs, NEON_TOK_IDENTIFIER, "expected superclass method name");
    name = neon_prs_makeidentconstant(prs, &prs->previous);
    neon_prs_parsenamedvar(prs, neon_prs_makesyntoken(prs, "this"), false);
    /* Superclasses super-get < Superclasses super-invoke */
    /*
    neon_prs_parsenamedvar(prs, neon_prs_makesyntoken(prs, "super"), false);
    neon_prs_emit2byte(prs, NEON_OP_INSTGETSUPER, name);
    */
    if(neon_prs_match(prs, NEON_TOK_PARENOPEN))
    {
        argc = neon_prs_parsearglist(prs);
        neon_prs_parsenamedvar(prs, neon_prs_makesyntoken(prs, "super"), false);
        neon_prs_emit2byte(prs, NEON_OP_INSTSUPERINVOKE, name);
        neon_prs_emit1byte(prs, argc);
    }
    else
    {
        neon_prs_parsenamedvar(prs, neon_prs_makesyntoken(prs, "super"), false);
        neon_prs_emit2byte(prs, NEON_OP_INSTGETSUPER, name);
    }
}

void neon_prs_rulethis(NeonAstParser* prs, bool canassign)
{
    (void)canassign;
    if(prs->currclass == NULL)
    {
        neon_prs_raiseerror(prs, "can't use 'this' outside of a class.");
        return;
    }
    neon_prs_rulevariable(prs, false);
}

void neon_prs_ruleunary(NeonAstParser* prs, bool canassign)
{
    NeonAstTokType ot;
    (void)canassign;
    ot = prs->previous.type;
    // Compile the operand.
    /* Compiling Expressions unary < Compiling Expressions unary-operand */
    //neon_prs_parseexpr(prs);
    neon_prs_parseprec(prs, NEON_PREC_UNARY);
    // Emit the operator instruction.
    switch(ot)
    {
        case NEON_TOK_EXCLAM:
            neon_prs_emit1byte(prs, NEON_OP_PRIMNOT);
            break;
        case NEON_TOK_MINUS:
            neon_prs_emit1byte(prs, NEON_OP_PRIMNEGATE);
            break;
        default:
            return;// Unreachable.
    }
}


void neon_prs_doassign(NeonAstParser* prs, int32_t getop, int32_t setop, int arg, bool canassign)
{
    if(canassign && neon_prs_match(prs, NEON_TOK_ASSIGN))
    {
        neon_prs_parseexpr(prs);
        neon_prs_emit2byte(prs, setop, (int32_t)arg);
    }
    else if(canassign && neon_prs_match(prs, NEON_TOK_INCREMENT))
    {
        if(getop == NEON_OP_PROPERTYGET /*|| getop == NEON_OP_GETPROPERTYGETTHIS*/)
        {
            neon_prs_emit1byte(prs, NEON_OP_DUP);
        }
        if(arg != -1)
        {
            neon_prs_emit2byte(prs, getop, arg);
        }
        else
        {
            neon_prs_emit2byte(prs, getop, 1);

        }
        neon_prs_emit2byte(prs, NEON_OP_PUSHONE, NEON_OP_PRIMADD);
        neon_prs_emit2byte(prs, setop, arg);
    }
    else if(canassign && neon_prs_match(prs, NEON_TOK_DECREMENT))
    {
        if(arg != -1)
        {
            neon_prs_emit2byte(prs, getop, arg);
        }
        else
        {
            neon_prs_emit2byte(prs, getop, 1);
        }
        neon_prs_emit2byte(prs, NEON_OP_PUSHONE, NEON_OP_PRIMSUBTRACT);
        neon_prs_emit2byte(prs, setop, arg);
    }
    else
    {
        if(arg != -1)
        {
            if(getop == NEON_OP_INDEXGET)
            {
                neon_prs_emit2byte(prs, getop, (int32_t)canassign);
            }
            else
            {
                neon_prs_emit2byte(prs, getop, arg);
            }
        }
        else
        {
            neon_prs_emit2byte(prs, getop, (int32_t)arg);
        }
    }
}

void neon_prs_rulearray(NeonAstParser* prs, bool canassign)
{
    int count;
    (void)canassign;
    count = 0;
    if(!neon_prs_checkcurrent(prs, NEON_TOK_BRACKETCLOSE))
    {
        do
        {
            neon_prs_parseexpr(prs);
            count++;
        } while(neon_prs_match(prs, NEON_TOK_COMMA));
    }
    neon_prs_consume(prs, NEON_TOK_BRACKETCLOSE, "expecteded ']' at end of list literal");
    neon_prs_emit2byte(prs, NEON_OP_MAKEARRAY, count);
}

void neon_prs_ruleindex(NeonAstParser* prs, bool canassign)
{
    bool willassign;
    (void)willassign;
    willassign = false;
    neon_prs_parseexpr(prs);
    neon_prs_consume(prs, NEON_TOK_BRACKETCLOSE, "expecteded ']' after indexing");
    if(neon_prs_checkcurrent(prs, NEON_TOK_ASSIGN))
    {
        willassign = true;
    }
    neon_prs_doassign(prs, NEON_OP_INDEXGET, NEON_OP_INDEXSET, -1, canassign);
}

void neon_prs_rulemap(NeonAstParser* prs, bool canassign)
{
    int count;
    (void)canassign;
    count = 0;
    neon_prs_consume(prs, NEON_TOK_BRACECLOSE, "expected '}' at end of map literal");
    neon_prs_emit2byte(prs, NEON_OP_MAKEMAP, count);
}

void neon_prs_ruleglobalstmt(NeonAstParser* prs, bool canassign)
{
    int iv;
    (void)canassign;
    iv = -1;
    if(neon_prs_match(prs, NEON_TOK_DOT))
    {
        neon_prs_consume(prs, NEON_TOK_IDENTIFIER, "expect name after '.'");
        iv = neon_prs_makeidentconstant(prs, &prs->previous);
        neon_prs_emit2byte(prs, NEON_OP_GLOBALSTMT, iv);
    }
    else
    {
        neon_prs_emit2byte(prs, NEON_OP_GLOBALSTMT, -1);
        if(neon_prs_match(prs, NEON_TOK_BRACKETOPEN))
        {
            neon_prs_ruleindex(prs, true);
            //neon_prs_doassign(prs, NEON_OP_INDEXGET, NEON_OP_INDEXSET, -1, true);
        }
    }
    neon_prs_skipsemicolon(prs);
}

void neon_prs_ruletypeof(NeonAstParser* prs, bool canassign)
{
    (void)canassign;
    neon_prs_parseexpr(prs);
    neon_prs_emit1byte(prs, NEON_OP_TYPEOF);
    neon_prs_skipsemicolon(prs);
}

NeonAstRule* neon_prs_setrule(NeonAstRule* rule, NeonAstParseFN prefix, NeonAstParseFN infix, NeonAstPrecedence precedence)
{
    rule->prefix = prefix;
    rule->infix = infix;
    rule->precedence = precedence;
    return rule;
}

NeonAstRule* neon_prs_getrule(NeonAstTokType type)
{
    static NeonAstRule dest;
    switch(type)
    {
        /* Compiling Expressions rules < Calls and Functions infix-left-paren */
        // [NEON_TOK_PARENOPEN]    = {grouping, NULL,   NEON_PREC_NONE},
        case NEON_TOK_PARENOPEN:
            {
                return neon_prs_setrule(&dest,  neon_prs_rulegrouping, neon_prs_rulecall, NEON_PREC_CALL );
            }
            break;
        case NEON_TOK_PARENCLOSE:
            {
                return neon_prs_setrule(&dest,  NULL, NULL, NEON_PREC_NONE );
            }
            break;
        case NEON_TOK_BRACEOPEN:
            {
                return neon_prs_setrule(&dest, neon_prs_rulemap, NULL, NEON_PREC_NONE );
            }
            break;
        case NEON_TOK_BRACECLOSE:
            {
                return neon_prs_setrule(&dest,  NULL, NULL, NEON_PREC_NONE );
            }
            break;
        case NEON_TOK_BRACKETOPEN:
            {
                return neon_prs_setrule(&dest,  neon_prs_rulearray, neon_prs_ruleindex, NEON_PREC_CALL );
            }
            break;
        case NEON_TOK_BRACKETCLOSE:
            {
                return neon_prs_setrule(&dest,  NULL, NULL, NEON_PREC_NONE );
            }
            break;
        case NEON_TOK_COMMA:
            {
                return neon_prs_setrule(&dest,  NULL, NULL, NEON_PREC_NONE );
            }
            break;
        /* Compiling Expressions rules < Classes and Instances table-dot */
        // [NEON_TOK_DOT]           = {NULL,     NULL,   NEON_PREC_NONE},
        case NEON_TOK_DOT:
            {
                return neon_prs_setrule(&dest,  NULL, neon_prs_ruledot, NEON_PREC_CALL );
            }
            break;
        case NEON_TOK_MINUS:
            {
                return neon_prs_setrule(&dest,  neon_prs_ruleunary, neon_prs_rulebinary, NEON_PREC_TERM );
            }
            break;
        case NEON_TOK_PLUS:
            {
                return neon_prs_setrule(&dest,  NULL, neon_prs_rulebinary, NEON_PREC_TERM );
            }
            break;
        case NEON_TOK_SEMICOLON:
            {
                return neon_prs_setrule(&dest,  NULL, NULL, NEON_PREC_NONE );
            }
            break;
        case NEON_TOK_NEWLINE:
            {
                return neon_prs_setrule(&dest,  NULL, NULL, NEON_PREC_NONE );
            }
            break;
        case NEON_TOK_SLASH:
            {
                return neon_prs_setrule(&dest,  NULL, neon_prs_rulebinary, NEON_PREC_FACTOR );
            }
            break;
        case NEON_TOK_STAR:
            {
                return neon_prs_setrule(&dest,  NULL, neon_prs_rulebinary, NEON_PREC_FACTOR );
            }
            break;
        case NEON_TOK_MODULO:
            {
                return neon_prs_setrule(&dest,  NULL, neon_prs_rulebinary, NEON_PREC_FACTOR );
            }
            break;
        case NEON_TOK_BINAND:
            {
                return neon_prs_setrule(&dest,  NULL, neon_prs_rulebinary, NEON_PREC_FACTOR );
            }
            break;
        case NEON_TOK_BINOR:
            {
                return neon_prs_setrule(&dest,  NULL, neon_prs_rulebinary, NEON_PREC_FACTOR );
            }
            break;
        case NEON_TOK_BINXOR:
            {
                return neon_prs_setrule(&dest,  NULL, neon_prs_rulebinary, NEON_PREC_FACTOR );
            }
            break;
        case NEON_TOK_SHIFTLEFT:
            {
                return neon_prs_setrule(&dest, NULL, neon_prs_rulebinary, NEON_PREC_SHIFT);
            }
            break;
        case NEON_TOK_SHIFTRIGHT:
            {
                return neon_prs_setrule(&dest, NULL, neon_prs_rulebinary, NEON_PREC_SHIFT);
            }
            break;
        case NEON_TOK_INCREMENT:
            {
                return neon_prs_setrule(&dest, NULL, NULL, NEON_PREC_NONE);
            }
            break;
        case NEON_TOK_DECREMENT:
            {
                return neon_prs_setrule(&dest, NULL, NULL, NEON_PREC_NONE);
            }
            break;
        /* Compiling Expressions rules < Types of Values table-not */
        // [NEON_TOK_EXCLAM]          = {NULL,     NULL,   NEON_PREC_NONE},
        case NEON_TOK_EXCLAM:
            {
                return neon_prs_setrule(&dest,  neon_prs_ruleunary, NULL, NEON_PREC_NONE );
            }
            break;
        /* Compiling Expressions rules < Types of Values table-equal */
        // [NEON_TOK_COMPNOTEQUAL]    = {NULL,     NULL,   NEON_PREC_NONE},
        case NEON_TOK_COMPNOTEQUAL:
            {
                return neon_prs_setrule(&dest,  NULL, neon_prs_rulebinary, NEON_PREC_EQUALITY );
            }
            break;
        case NEON_TOK_ASSIGN:
            {
                return neon_prs_setrule(&dest,  NULL, NULL, NEON_PREC_NONE );
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
                return neon_prs_setrule(&dest,  NULL, neon_prs_rulebinary, NEON_PREC_EQUALITY );
            }
            break;
        case NEON_TOK_COMPGREATERTHAN:
            {
                return neon_prs_setrule(&dest,  NULL, neon_prs_rulebinary, NEON_PREC_COMPARISON );
            }
            break;
        case NEON_TOK_COMPGREATEREQUAL:
            {
                return neon_prs_setrule(&dest,  NULL, neon_prs_rulebinary, NEON_PREC_COMPARISON );
            }
            break;
        case NEON_TOK_COMPLESSTHAN:
            {
                return neon_prs_setrule(&dest,  NULL, neon_prs_rulebinary, NEON_PREC_COMPARISON );
            }
            break;
        case NEON_TOK_COMPLESSEQUAL:
            {
                return neon_prs_setrule(&dest,  NULL, neon_prs_rulebinary, NEON_PREC_COMPARISON );
            }
            break;
        /* Compiling Expressions rules < Global Variables table-identifier */
        // [NEON_TOK_IDENTIFIER]    = {NULL,     NULL,   NEON_PREC_NONE},
        case NEON_TOK_IDENTIFIER:
            {
                return neon_prs_setrule(&dest,  neon_prs_rulevariable, NULL, NEON_PREC_NONE );
            }
            break;
        /* Compiling Expressions rules < Strings table-string */
        // [NEON_TOK_STRING]        = {NULL,     NULL,   NEON_PREC_NONE},
        case NEON_TOK_STRING:
            {
                return neon_prs_setrule(&dest,  neon_prs_rulestring, NULL, NEON_PREC_NONE );
            }
            break;
        case NEON_TOK_NUMBER:
            {
                return neon_prs_setrule(&dest,  neon_prs_rulenumber, NULL, NEON_PREC_NONE );
            }
            break;
        /* Compiling Expressions rules < Jumping Back and Forth table-and */
        // [NEON_TOK_KWAND]           = {NULL,     NULL,   NEON_PREC_NONE},
        case NEON_TOK_KWBREAK:
            {
                return neon_prs_setrule(&dest, NULL, NULL, NEON_PREC_NONE);
            }
            break;
        case NEON_TOK_KWCONTINUE:
            {
                return neon_prs_setrule(&dest,  NULL, NULL, NEON_PREC_NONE );
            }
            break;
        case NEON_TOK_KWAND:
            {
                return neon_prs_setrule(&dest,  NULL, neon_prs_ruleand, NEON_PREC_AND );
            }
            break;
        case NEON_TOK_KWCLASS:
            {
                return neon_prs_setrule(&dest,  NULL, NULL, NEON_PREC_NONE );
            }
            break;
        case NEON_TOK_KWELSE:
            {
                return neon_prs_setrule(&dest,  NULL, NULL, NEON_PREC_NONE );
            }
            break;
        /* Compiling Expressions rules < Types of Values table-false */
        // [NEON_TOK_KWFALSE]         = {NULL,     NULL,   NEON_PREC_NONE},
        case NEON_TOK_KWFALSE:
            {
                return neon_prs_setrule(&dest,  neon_prs_ruleliteral, NULL, NEON_PREC_NONE );
            }
            break;
        case NEON_TOK_KWFOR:
            {
                return neon_prs_setrule(&dest,  NULL, NULL, NEON_PREC_NONE );
            }
            break;
        case NEON_TOK_KWFUNCTION:
            {
                return neon_prs_setrule(&dest,  NULL, NULL, NEON_PREC_NONE );
            }
            break;
        case NEON_TOK_KWIF:
            {
                return neon_prs_setrule(&dest,  NULL, NULL, NEON_PREC_NONE );
            }
            break;
        /* Compiling Expressions rules < Types of Values table-nil
        * [NEON_TOK_KWNIL]           = {NULL,     NULL,   NEON_PREC_NONE},
        */
        case NEON_TOK_KWNIL:
            {
                return neon_prs_setrule(&dest,  neon_prs_ruleliteral, NULL, NEON_PREC_NONE );
            }
            break;
        /* Compiling Expressions rules < Jumping Back and Forth table-or
        * [NEON_TOK_KWOR]            = {NULL,     NULL,   NEON_PREC_NONE},
        */
        case NEON_TOK_KWOR:
            {
                return neon_prs_setrule(&dest,  NULL, neon_prs_ruleor, NEON_PREC_OR );
            }
            break;
        case NEON_TOK_KWDEBUGPRINT:
            {
                return neon_prs_setrule(&dest,  NULL, NULL, NEON_PREC_NONE );
            }
            break;
        case NEON_TOK_KWGLOBAL:
            {
                return neon_prs_setrule(&dest, neon_prs_ruleglobalstmt, NULL, NEON_PREC_NONE );
            }
            break;
        case NEON_TOK_KWTYPEOF:
            {
                return neon_prs_setrule(&dest, neon_prs_ruletypeof, NULL, NEON_PREC_NONE);
            }
            break;
        case NEON_TOK_KWRETURN:
            {
                return neon_prs_setrule(&dest,  NULL, NULL, NEON_PREC_NONE );
            }
            break;
        /* Compiling Expressions rules < Superclasses table-super
        * [NEON_TOK_KWSUPER]         = {NULL,     NULL,   NEON_PREC_NONE},
        */
        case NEON_TOK_KWSUPER:
            {
                return neon_prs_setrule(&dest,  neon_prs_rulesuper, NULL, NEON_PREC_NONE );
            }
            break;
        /* Compiling Expressions rules < Methods and Initializers table-this
        * [NEON_TOK_KWTHIS]          = {NULL,     NULL,   NEON_PREC_NONE},
        */
        case NEON_TOK_KWTHIS:
            {
                return neon_prs_setrule(&dest,  neon_prs_rulethis, NULL, NEON_PREC_NONE );
            }
            break;
        /* Compiling Expressions rules < Types of Values table-true
        * [NEON_TOK_KWTRUE]          = {NULL,     NULL,   NEON_PREC_NONE},
        */
        case NEON_TOK_KWTRUE:
            {
                return neon_prs_setrule(&dest,  neon_prs_ruleliteral, NULL, NEON_PREC_NONE );
            }
            break;
        case NEON_TOK_KWVAR:
            {
                return neon_prs_setrule(&dest,  NULL, NULL, NEON_PREC_NONE );
            }
            break;
        case NEON_TOK_KWWHILE:
            {
                return neon_prs_setrule(&dest,  NULL, NULL, NEON_PREC_NONE );
            }
            break;
        case NEON_TOK_ERROR:
            {
                return neon_prs_setrule(&dest,  NULL, NULL, NEON_PREC_NONE );
            }
            break;
        case NEON_TOK_EOF:
            {
                return neon_prs_setrule(&dest,  NULL, NULL, NEON_PREC_NONE );
            }
            break;
    }
    return NULL;
}

void neon_prs_parseprec(NeonAstParser* prs, NeonAstPrecedence precedence)
{
    bool canassign;
    NeonAstParseFN infixrule;
    NeonAstParseFN prefixrule;
    neon_prs_advance(prs);
    prefixrule = neon_prs_getrule(prs->previous.type)->prefix;
    if(prefixrule == NULL)
    {
        neon_prs_raiseerror(prs, "expected expression");
        return;
    }
    canassign = precedence <= NEON_PREC_ASSIGNMENT;
    prefixrule(prs, canassign);
    while(precedence <= neon_prs_getrule(prs->current.type)->precedence)
    {
        neon_prs_advance(prs);
        infixrule = neon_prs_getrule(prs->previous.type)->infix;
        /* Compiling Expressions infix < Global Variables infix-rule */
        //infixrule();
        infixrule(prs, canassign);
    }
    if(canassign && neon_prs_match(prs, NEON_TOK_ASSIGN))
    {
        neon_prs_raiseerror(prs, "invalid assignment target");
    }
}

void neon_prs_parseexpr(NeonAstParser* prs)
{
    /* Compiling Expressions expression < Compiling Expressions expression-body
    // What goes here?
    */
    neon_prs_parseprec(prs, NEON_PREC_ASSIGNMENT);
}

void neon_prs_parseblock(NeonAstParser* prs)
{
    while(!neon_prs_checkcurrent(prs, NEON_TOK_BRACECLOSE) && !neon_prs_checkcurrent(prs, NEON_TOK_EOF))
    {
        neon_prs_parsedecl(prs);
    }
    neon_prs_consume(prs, NEON_TOK_BRACECLOSE, "expected '}' after block");
}

void neon_prs_parsefunction(NeonAstParser* prs, NeonAstFuncType type)
{
    int i;
    int32_t constant;
    NeonObjScriptFunction* fn;
    NeonAstCompiler compiler;
    neon_prs_compilerinit(prs, &compiler, type);
    neon_prs_scopebegin(prs);// [no-end-scope]
    neon_prs_consume(prs, NEON_TOK_PARENOPEN, "expected '(' after function name");
    if(!neon_prs_checkcurrent(prs, NEON_TOK_PARENCLOSE))
    {
        do
        {
            prs->currcompiler->compiledfn->arity++;
            if(prs->currcompiler->compiledfn->arity > 255)
            {
                neon_prs_raiseatcurrent(prs, "function has too many arguments declared");
            }
            constant = neon_prs_parsevarname(prs, "expected parameter name");
            neon_prs_emitdefvar(prs, constant);
        } while(neon_prs_match(prs, NEON_TOK_COMMA));
    }
    neon_prs_consume(prs, NEON_TOK_PARENCLOSE, "expected ')' after parameters");
    neon_prs_consume(prs, NEON_TOK_BRACEOPEN, "expected '{' before function body");
    neon_prs_parseblock(prs);
    fn = neon_prs_compilerfinish(prs, false);
    /* Calls and Functions compile-function < Closures emit-closure */
    // neon_prs_emit2byte(prs, NEON_OP_PUSHCONST, neon_prs_makeconstant(prs, neon_value_fromobject(fn)));
    neon_prs_emit2byte(prs, NEON_OP_CLOSURE, neon_prs_makeconstant(prs, neon_value_fromobject(fn)));
    for(i = 0; i < fn->upvaluecount; i++)
    {
        neon_prs_emit1byte(prs, compiler.compupvals[i].islocal ? 1 : 0);
        neon_prs_emit1byte(prs, compiler.compupvals[i].index);
    }
}

void neon_prs_parsemethod(NeonAstParser* prs)
{
    int32_t constant;
    NeonAstFuncType type;
    neon_prs_consume(prs, NEON_TOK_IDENTIFIER, "expect method name.");
    constant = neon_prs_makeidentconstant(prs, &prs->previous);

    /* Methods and Initializers method-body < Methods and Initializers method-type */
    //type = NEON_TYPE_FUNCTION;
    type = NEON_TYPE_METHOD;
    if(prs->previous.length == 4 && memcmp(prs->previous.start, "init", 4) == 0)
    {
        type = NEON_TYPE_INITIALIZER;
    }
    neon_prs_parsefunction(prs, type);
    neon_prs_emit2byte(prs, NEON_OP_METHOD, constant);
}

void neon_prs_parseclassdecl(NeonAstParser* prs)
{
    int32_t nameconstant;
    NeonAstToken classname;
    NeonAstClassCompiler classcompiler;
    neon_prs_consume(prs, NEON_TOK_IDENTIFIER, "expect class name.");
    classname = prs->previous;
    nameconstant = neon_prs_makeidentconstant(prs, &prs->previous);
    neon_prs_parsevarident(prs);
    neon_prs_emit2byte(prs, NEON_OP_CLASS, nameconstant);
    neon_prs_emitdefvar(prs, nameconstant);
    classcompiler.hassuperclass = false;
    classcompiler.enclosing = prs->currclass;
    prs->currclass = &classcompiler;
    if(neon_prs_match(prs, NEON_TOK_COMPLESSTHAN))
    {
        neon_prs_consume(prs, NEON_TOK_IDENTIFIER, "expect superclass name");
        neon_prs_rulevariable(prs, false);
        if(neon_prs_identsequal(&classname, &prs->previous))
        {
            neon_prs_raiseerror(prs, "a class cannot inherit from itself");
        }
        neon_prs_scopebegin(prs);
        neon_prs_addlocal(prs, neon_prs_makesyntoken(prs, "super"));
        neon_prs_emitdefvar(prs, 0);
        neon_prs_parsenamedvar(prs, classname, false);
        neon_prs_emit1byte(prs, NEON_OP_INHERIT);
        classcompiler.hassuperclass = true;
    }
    neon_prs_parsenamedvar(prs, classname, false);
    neon_prs_consume(prs, NEON_TOK_BRACEOPEN, "expected '{' before class body");
    while(!neon_prs_checkcurrent(prs, NEON_TOK_BRACECLOSE) && !neon_prs_checkcurrent(prs, NEON_TOK_EOF))
    {
        neon_prs_parsemethod(prs);
    }
    neon_prs_consume(prs, NEON_TOK_BRACECLOSE, "expected '}' after class body");
    neon_prs_emit1byte(prs, NEON_OP_POP);
    if(classcompiler.hassuperclass)
    {
        neon_prs_scopeend(prs);
    }
    prs->currclass = prs->currclass->enclosing;
}

void neon_prs_parsefuncdecl(NeonAstParser* prs)
{
    int32_t global;
    global = neon_prs_parsevarname(prs, "expected function name");
    neon_prs_markinit(prs);
    neon_prs_parsefunction(prs, NEON_TYPE_FUNCTION);
    neon_prs_emitdefvar(prs, global);
}

void neon_prs_parsevardecl(NeonAstParser* prs)
{
    int32_t global;
    global = neon_prs_parsevarname(prs, "expected variable name");
    if(neon_prs_match(prs, NEON_TOK_ASSIGN))
    {
        neon_prs_parseexpr(prs);
    }
    else
    {
        neon_prs_emit1byte(prs, NEON_OP_PUSHNIL);
    }
    neon_prs_skipsemicolon(prs);
    neon_prs_emitdefvar(prs, global);
}

void neon_prs_parseexprstmt(NeonAstParser* prs)
{
    neon_prs_parseexpr(prs);
    neon_prs_skipsemicolon(prs);
    if(!prs->iseval)
    {
        neon_prs_emit1byte(prs, NEON_OP_POP);
    }
}

void neon_prs_parseforstmt(NeonAstParser* prs)
{
    int loopstart;
    int exitjump;
    int bodyjump;
    int incrementstart;
    NeonAstLoop loop;
    neon_prs_scopebegin(prs);
    neon_prs_startloop(prs, &loop);
    neon_prs_consume(prs, NEON_TOK_PARENOPEN, "expected '(' after 'for'");
    if(neon_prs_match(prs, NEON_TOK_SEMICOLON))
    {
        // No initializer.
    }
    else if(neon_prs_match(prs, NEON_TOK_KWVAR))
    {
        neon_prs_parsevardecl(prs);
    }
    else
    {
        neon_prs_parseexprstmt(prs);
    }
    loopstart = neon_prs_currentchunk(prs)->count;
    exitjump = -1;
    if(!neon_prs_match(prs, NEON_TOK_SEMICOLON))
    {
        neon_prs_parseexpr(prs);
        neon_prs_consume(prs, NEON_TOK_SEMICOLON, "expected ';' after 'for' loop condition");
        // Jump out of the loop if the condition is false.
        exitjump = neon_prs_emitjump(prs, NEON_OP_JUMPIFFALSE);
        neon_prs_emit1byte(prs, NEON_OP_POP);// Condition
    }
    if(!neon_prs_match(prs, NEON_TOK_PARENCLOSE))
    {
        bodyjump = neon_prs_emitjump(prs, NEON_OP_JUMPNOW);
        incrementstart = neon_prs_currentchunk(prs)->count;
        // when we 'continue' in for loop, we want to jump here
        prs->currcompiler->loop->start = incrementstart;
        neon_prs_parseexpr(prs);
        neon_prs_emit1byte(prs, NEON_OP_POP);
        neon_prs_consume(prs, NEON_TOK_PARENCLOSE, "expected ')' after 'for' clauses");
        neon_prs_emitloop(prs, loopstart);
        loopstart = incrementstart;
        neon_prs_emitpatchjump(prs, bodyjump);
    }
    prs->currcompiler->loop->body = neon_prs_currentchunk(prs)->count;
    neon_prs_parsestmt(prs);
    neon_prs_emitloop(prs, loopstart);
    if(exitjump != -1)
    {
        neon_prs_emitpatchjump(prs, exitjump);
        neon_prs_emit1byte(prs, NEON_OP_POP);
    }
    neon_prs_endloop(prs);
    neon_prs_scopeend(prs);
}

void neon_prs_parsewhilestmt(NeonAstParser* prs)
{
    int loopstart;
    int exitjump;
    NeonAstLoop loop;
    neon_prs_startloop(prs, &loop);
    loopstart = neon_prs_currentchunk(prs)->count;
    neon_prs_consume(prs, NEON_TOK_PARENOPEN, "expected '(' after 'while'");
    neon_prs_parseexpr(prs);
    neon_prs_consume(prs, NEON_TOK_PARENCLOSE, "expected ')' after condition");
    exitjump = neon_prs_emitjump(prs, NEON_OP_JUMPIFFALSE);
    neon_prs_emit1byte(prs, NEON_OP_POP);
    prs->currcompiler->loop->body = neon_prs_currentchunk(prs)->count;
    neon_prs_parsestmt(prs);
    neon_prs_emitloop(prs, loopstart);
    neon_prs_emitpatchjump(prs, exitjump);
    neon_prs_emit1byte(prs, NEON_OP_POP);
    neon_prs_endloop(prs);
}

void neon_prs_parsebreakstmt(NeonAstParser* prs)
{
    if(prs->currcompiler->loop == NULL)
    {
        neon_prs_raiseerror(prs, "cannot use 'break' outside of a loop");
        return;
    }
    neon_prs_skipsemicolon(prs);
    neon_prs_discardlocals(prs, prs->currcompiler);
    neon_prs_emitjump(prs, NEON_OP_PSEUDOBREAK);
}

void neon_prs_parsecontinuestmt(NeonAstParser* prs)
{
    if(prs->currcompiler->loop == NULL)
    {
        neon_prs_raiseerror(prs, "cannot use 'continue' outside of a loop");
        return;
    }
    neon_prs_skipsemicolon(prs);
    neon_prs_discardlocals(prs, prs->currcompiler);
    neon_prs_emitloop(prs, prs->currcompiler->loop->start);
}

void neon_prs_parseifstmt(NeonAstParser* prs)
{
    int thenjump;
    int elsejump;
    neon_prs_consume(prs, NEON_TOK_PARENOPEN, "expected '(' after 'if'");
    neon_prs_parseexpr(prs);
    neon_prs_consume(prs, NEON_TOK_PARENCLOSE, "expect ')' after condition");// [paren]
    thenjump = neon_prs_emitjump(prs, NEON_OP_JUMPIFFALSE);
    neon_prs_emit1byte(prs, NEON_OP_POP);
    neon_prs_parsestmt(prs);
    elsejump = neon_prs_emitjump(prs, NEON_OP_JUMPNOW);
    neon_prs_emitpatchjump(prs, thenjump);
    neon_prs_emit1byte(prs, NEON_OP_POP);
    if(neon_prs_match(prs, NEON_TOK_KWELSE))
    {
        neon_prs_parsestmt(prs);
    }
    neon_prs_emitpatchjump(prs, elsejump);
}

void neon_prs_parsedebugprintstmt(NeonAstParser* prs)
{
    neon_prs_parseexpr(prs);
    //neon_prs_consume(prs, NEON_TOK_SEMICOLON, "expect ';' after value.");
    neon_prs_skipsemicolon(prs);
    neon_prs_emit1byte(prs, NEON_OP_DEBUGPRINT);
}

void neon_prs_parsereturnstmt(NeonAstParser* prs)
{
    if(prs->currcompiler->type == NEON_TYPE_SCRIPT)
    {
        neon_prs_raiseerror(prs, "cannot return from top-level code");
    }
    if(neon_prs_match(prs, NEON_TOK_SEMICOLON) || neon_prs_match(prs, NEON_TOK_NEWLINE))
    {
        neon_prs_emitreturn(prs, false);
    }
    else
    {
        if(prs->currcompiler->type == NEON_TYPE_INITIALIZER)
        {
            neon_prs_raiseerror(prs, "cannot return a value from an initializer");
        }
        neon_prs_parseexpr(prs);
        //neon_prs_consume(prs, NEON_TOK_SEMICOLON, "expect ';' after return value.");
        neon_prs_skipsemicolon(prs);
        neon_prs_emit1byte(prs, NEON_OP_RETURN);
    }
}

void neon_prs_synchronize(NeonAstParser* prs)
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
        neon_prs_advance(prs);
    }
}

void neon_prs_parsedecl(NeonAstParser* prs)
{
    if(neon_prs_match(prs, NEON_TOK_KWCLASS))
    {
        neon_prs_parseclassdecl(prs);
    }
    else if(neon_prs_match(prs, NEON_TOK_KWFUNCTION))
    {
        neon_prs_parsefuncdecl(prs);
    }
    else if(neon_prs_match(prs, NEON_TOK_KWVAR))
    {
        neon_prs_parsevardecl(prs);
    }
    else
    {
        neon_prs_parsestmt(prs);
    }
    /* Global Variables declaration < Global Variables match-var */
    // neon_prs_parsestmt(prs);
    if(prs->panicmode)
    {
        neon_prs_synchronize(prs);
    }
}

void neon_prs_parsestmt(NeonAstParser* prs)
{
    if(neon_prs_match(prs, NEON_TOK_KWGLOBAL))
    {
        neon_prs_ruleglobalstmt(prs, false);
    }
    else if(neon_prs_match(prs, NEON_TOK_KWDEBUGPRINT))
    {
        neon_prs_parsedebugprintstmt(prs);
    }
    else if(neon_prs_match(prs, NEON_TOK_KWBREAK))
    {
        neon_prs_parsebreakstmt(prs);
    }
    else if(neon_prs_match(prs, NEON_TOK_KWCONTINUE))
    {
        neon_prs_parsecontinuestmt(prs);
    }
    else if(neon_prs_match(prs, NEON_TOK_KWFOR))
    {
        neon_prs_parseforstmt(prs);
    }
    else if(neon_prs_match(prs, NEON_TOK_KWIF))
    {
        neon_prs_parseifstmt(prs);
    }
    else if(neon_prs_match(prs, NEON_TOK_KWRETURN))
    {
        neon_prs_parsereturnstmt(prs);
    }
    else if(neon_prs_match(prs, NEON_TOK_KWWHILE))
    {
        neon_prs_parsewhilestmt(prs);
    }
    else if(neon_prs_match(prs, NEON_TOK_BRACEOPEN))
    {
        neon_prs_scopebegin(prs);
        neon_prs_parseblock(prs);
        neon_prs_scopeend(prs);
    }
    else
    {
        neon_prs_parseexprstmt(prs);
    }
}



