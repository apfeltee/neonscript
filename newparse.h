
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


static jmp_buf prs_jmpbuffer;
static NeonAstParseRule rules[NEON_TOK_EOF + 1];


static NeonAstTokType operators[]=
{
    NEON_TOK_PLUS, NEON_TOK_MINUS, NEON_TOK_STAR, NEON_TOK_PERCENT, NEON_TOK_SLASH,
    NEON_TOK_SHARP, NEON_TOK_BANG, NEON_TOK_LESSTHAN, NEON_TOK_LESSEQUAL, NEON_TOK_GREATERTHAN,
    NEON_TOK_GREATEREQUAL, NEON_TOK_EQUAL, NEON_TOK_BRACKETOPEN, NEON_TOK_EOF
};


static bool didsetuprules;
static void neon_astparser_setuprules();
static void neon_astparser_sync(NeonAstParser* prs);

static NeonAstExpression *neon_astparser_parseblock(NeonAstParser *prs);
static NeonAstExpression *neon_astparser_parseprecedence(NeonAstParser *prs, NeonAstPrecedence precedence, bool err, bool ignsemi);
static NeonAstExpression *neon_astparser_parselambda(NeonAstParser *prs, NeonAstFunctionExpr *lambda);
static void neon_astparser_parseparameters(NeonAstParser *prs, NeonAstParamList *parameters);
static NeonAstExpression *neon_astparser_parseexpression(NeonAstParser *prs, bool ignsemi);
static NeonAstExpression *neon_astparser_parsevar_declaration(NeonAstParser *prs, bool ignsemi);
static NeonAstExpression *neon_astparser_parseif(NeonAstParser *prs);
static NeonAstExpression *neon_astparser_parsefor(NeonAstParser *prs);
static NeonAstExpression *neon_astparser_parsewhile(NeonAstParser *prs);
static NeonAstExpression *neon_astparser_parsereturn(NeonAstParser *prs);
static NeonAstExpression *neon_astparser_parsefield(NeonAstParser *prs, NeonString *name, bool isstatic);
static NeonAstExpression *neon_astparser_parsemethod(NeonAstParser *prs, bool isstatic);
static NeonAstExpression *neon_astparser_parseclass(NeonAstParser *prs);
static NeonAstExpression *neon_astparser_parsestatement(NeonAstParser *prs);
static NeonAstExpression *neon_astparser_parsedeclaration(NeonAstParser *prs);

static NeonAstExpression *neon_astparser_rulenumber(NeonAstParser *prs, bool canassign);
static NeonAstExpression *neon_astparser_rulegroupingorlambda(NeonAstParser *prs, bool canassign);
static NeonAstExpression *neon_astparser_rulecall(NeonAstParser *prs, NeonAstExpression *prev, bool canassign);
static NeonAstExpression *neon_astparser_ruleunary(NeonAstParser *prs, bool canassign);
static NeonAstExpression *neon_astparser_rulebinary(NeonAstParser *prs, NeonAstExpression *prev, bool canassign);
static NeonAstExpression *neon_astparser_ruleand(NeonAstParser *prs, NeonAstExpression *prev, bool canassign);
static NeonAstExpression *neon_astparser_ruleor(NeonAstParser *prs, NeonAstExpression *prev, bool canassign);
static NeonAstExpression *neon_astparser_rulenull_filter(NeonAstParser *prs, NeonAstExpression *prev, bool canassign);
static NeonAstExpression *neon_astparser_rulecompound(NeonAstParser *prs, NeonAstExpression *prev, bool canassign);
static NeonAstExpression *neon_astparser_ruleliteral(NeonAstParser *prs, bool canassign);
static NeonAstExpression *neon_astparser_rulestring(NeonAstParser *prs, bool canassign);
static NeonAstExpression *neon_astparser_ruleinterpolation(NeonAstParser *prs, bool canassign);
static NeonAstExpression *neon_astparser_ruleobject(NeonAstParser *prs, bool canassign);
static NeonAstExpression *neon_astparser_rulevarexprbase(NeonAstParser *prs, bool canassign, bool isnew);
static NeonAstExpression *neon_astparser_rulevarexpr(NeonAstParser *prs, bool canassign);
static NeonAstExpression *neon_astparser_rulenewexpr(NeonAstParser *prs, bool canassign);
static NeonAstExpression *neon_astparser_ruledot(NeonAstParser *prs, NeonAstExpression *previous, bool canassign);
static NeonAstExpression *neon_astparser_rulerange(NeonAstParser *prs, NeonAstExpression *previous, bool canassign);
static NeonAstExpression *neon_astparser_ruleternary(NeonAstParser *prs, NeonAstExpression *previous, bool canassign);
static NeonAstExpression *neon_astparser_rulearray(NeonAstParser *prs, bool canassign);
static NeonAstExpression *neon_astparser_rulesubscript(NeonAstParser *prs, NeonAstExpression *previous, bool canassign);
static NeonAstExpression *neon_astparser_rulethis(NeonAstParser *prs, bool canassign);
static NeonAstExpression *neon_astparser_rulesuper(NeonAstParser *prs, bool canassign);
static NeonAstExpression *neon_astparser_rulereference(NeonAstParser *prs, bool canassign);
static NeonAstExpression *neon_astparser_rulenothing(NeonAstParser *prs, bool canassign);
static NeonAstExpression *neon_astparser_rulefunction(NeonAstParser *prs, bool canassign);

#if defined(__cplusplus)
    #define TIN_MAKERULE(...) NeonAstParseRule{__VA_ARGS__}
#else
    #define TIN_MAKERULE(...) (NeonAstParseRule){__VA_ARGS__}
#endif

static void neon_astparser_setuprules()
{
    rules[NEON_TOK_PARENOPEN] = TIN_MAKERULE( neon_astparser_rulegroupingorlambda, neon_astparser_rulecall, TINPREC_CALL );
    rules[NEON_TOK_PLUS] = TIN_MAKERULE( NULL, neon_astparser_rulebinary, TINPREC_TERM );
    rules[NEON_TOK_MINUS] = TIN_MAKERULE( neon_astparser_ruleunary, neon_astparser_rulebinary, TINPREC_TERM );
    rules[NEON_TOK_BANG] = TIN_MAKERULE( neon_astparser_ruleunary, neon_astparser_rulebinary, TINPREC_TERM );
    rules[NEON_TOK_STAR] = TIN_MAKERULE( NULL, neon_astparser_rulebinary, TINPREC_FACTOR );
    rules[NEON_TOK_DOUBLESTAR] = TIN_MAKERULE( NULL, neon_astparser_rulebinary, TINPREC_FACTOR );
    rules[NEON_TOK_SLASH] = TIN_MAKERULE( NULL, neon_astparser_rulebinary, TINPREC_FACTOR );
    rules[NEON_TOK_SHARP] = TIN_MAKERULE( NULL, neon_astparser_rulebinary, TINPREC_FACTOR );
    rules[NEON_TOK_STAR] = TIN_MAKERULE( NULL, neon_astparser_rulebinary, TINPREC_FACTOR );
    rules[NEON_TOK_STAR] = TIN_MAKERULE( NULL, neon_astparser_rulebinary, TINPREC_FACTOR );
    rules[NEON_TOK_BAR] = TIN_MAKERULE( NULL, neon_astparser_rulebinary, TINPREC_BOR );
    rules[NEON_TOK_AMPERSAND] = TIN_MAKERULE( NULL, neon_astparser_rulebinary, TINPREC_BAND );
    rules[NEON_TOK_TILDE] = TIN_MAKERULE( neon_astparser_ruleunary, NULL, TINPREC_UNARY );
    rules[NEON_TOK_CARET] = TIN_MAKERULE( NULL, neon_astparser_rulebinary, TINPREC_BOR );
    rules[NEON_TOK_SHIFTLEFT] = TIN_MAKERULE( NULL, neon_astparser_rulebinary, TINPREC_SHIFT );
    rules[NEON_TOK_SHIFTRIGHT] = TIN_MAKERULE( NULL, neon_astparser_rulebinary, TINPREC_SHIFT );
    rules[NEON_TOK_PERCENT] = TIN_MAKERULE( NULL, neon_astparser_rulebinary, TINPREC_FACTOR );
    rules[NEON_TOK_KWIS] = TIN_MAKERULE( NULL, neon_astparser_rulebinary, TINPREC_IS );
    rules[NEON_TOK_NUMBER] = TIN_MAKERULE( neon_astparser_rulenumber, NULL, TINPREC_NONE );
    rules[NEON_TOK_KWTRUE] = TIN_MAKERULE( neon_astparser_ruleliteral, NULL, TINPREC_NONE );
    rules[NEON_TOK_KWFALSE] = TIN_MAKERULE( neon_astparser_ruleliteral, NULL, TINPREC_NONE );
    rules[NEON_TOK_KWNULL] = TIN_MAKERULE( neon_astparser_ruleliteral, NULL, TINPREC_NONE );
    rules[NEON_TOK_BANGEQUAL] = TIN_MAKERULE( NULL, neon_astparser_rulebinary, TINPREC_EQUALITY );
    rules[NEON_TOK_EQUAL] = TIN_MAKERULE( NULL, neon_astparser_rulebinary, TINPREC_EQUALITY );
    rules[NEON_TOK_GREATERTHAN] = TIN_MAKERULE( NULL, neon_astparser_rulebinary, TINPREC_COMPARISON );
    rules[NEON_TOK_GREATEREQUAL] = TIN_MAKERULE( NULL, neon_astparser_rulebinary, TINPREC_COMPARISON );
    rules[NEON_TOK_LESSTHAN] = TIN_MAKERULE( NULL, neon_astparser_rulebinary, TINPREC_COMPARISON );
    rules[NEON_TOK_LESSEQUAL] = TIN_MAKERULE( NULL, neon_astparser_rulebinary, TINPREC_COMPARISON );
    rules[NEON_TOK_STRING] = TIN_MAKERULE( neon_astparser_rulestring, NULL, TINPREC_NONE );
    rules[NEON_TOK_STRINTERPOL] = TIN_MAKERULE( neon_astparser_ruleinterpolation, NULL, TINPREC_NONE );
    rules[NEON_TOK_IDENT] = TIN_MAKERULE( neon_astparser_rulevarexpr, NULL, TINPREC_NONE );
    rules[NEON_TOK_KWNEW] = TIN_MAKERULE( neon_astparser_rulenewexpr, NULL, TINPREC_NONE );
    rules[NEON_TOK_PLUSEQUAL] = TIN_MAKERULE( NULL, neon_astparser_rulecompound, TINPREC_COMPOUND );
    rules[NEON_TOK_MINUSEQUAL] = TIN_MAKERULE( NULL, neon_astparser_rulecompound, TINPREC_COMPOUND );
    rules[NEON_TOK_STAREQUAL] = TIN_MAKERULE( NULL, neon_astparser_rulecompound, TINPREC_COMPOUND );
    rules[NEON_TOK_SLASHEQUAL] = TIN_MAKERULE( NULL, neon_astparser_rulecompound, TINPREC_COMPOUND );
    rules[NEON_TOK_SHARPEQUAL] = TIN_MAKERULE( NULL, neon_astparser_rulecompound, TINPREC_COMPOUND );
    rules[NEON_TOK_PERCENTEQUAL] = TIN_MAKERULE( NULL, neon_astparser_rulecompound, TINPREC_COMPOUND );
    rules[NEON_TOK_CARETEQUAL] = TIN_MAKERULE( NULL, neon_astparser_rulecompound, TINPREC_COMPOUND );
    rules[NEON_TOK_ASSIGNEQUAL] = TIN_MAKERULE( NULL, neon_astparser_rulecompound, TINPREC_COMPOUND );
    rules[NEON_TOK_AMPERSANDEQUAL] = TIN_MAKERULE( NULL, neon_astparser_rulecompound, TINPREC_COMPOUND );
    rules[NEON_TOK_DOUBLEPLUS] = TIN_MAKERULE( NULL, neon_astparser_rulecompound, TINPREC_COMPOUND );
    rules[NEON_TOK_DOUBLEMINUS] = TIN_MAKERULE( NULL, neon_astparser_rulecompound, TINPREC_COMPOUND );
    rules[NEON_TOK_DOUBLEAMPERSAND] = TIN_MAKERULE( NULL, neon_astparser_ruleand, TINPREC_AND );
    rules[NEON_TOK_DOUBLEBAR] = TIN_MAKERULE( NULL, neon_astparser_ruleor, TINPREC_AND );
    rules[NEON_TOK_DOUBLEQUESTION] = TIN_MAKERULE( NULL, neon_astparser_rulenull_filter, TINPREC_NULL );
    rules[NEON_TOK_DOT] = TIN_MAKERULE( NULL, neon_astparser_ruledot, TINPREC_CALL );
    rules[NEON_TOK_SMALLARROW] = TIN_MAKERULE( NULL, neon_astparser_ruledot, TINPREC_CALL );
    rules[NEON_TOK_DOUBLEDOT] = TIN_MAKERULE( NULL, neon_astparser_rulerange, TINPREC_RANGE );
    rules[NEON_TOK_TRIPLEDOT] = TIN_MAKERULE( neon_astparser_rulevarexpr, NULL, TINPREC_ASSIGNMENT );
    rules[NEON_TOK_BRACKETOPEN] = TIN_MAKERULE( neon_astparser_rulearray, neon_astparser_rulesubscript, TINPREC_NONE );
    rules[NEON_TOK_BRACEOPEN] = TIN_MAKERULE( neon_astparser_ruleobject, NULL, TINPREC_NONE );
    rules[NEON_TOK_KWTHIS] = TIN_MAKERULE( neon_astparser_rulethis, NULL, TINPREC_NONE );
    rules[NEON_TOK_KWSUPER] = TIN_MAKERULE( neon_astparser_rulesuper, NULL, TINPREC_NONE );
    rules[NEON_TOK_QUESTION] = TIN_MAKERULE( NULL, neon_astparser_ruleternary, TINPREC_EQUALITY );
    rules[NEON_TOK_KWREF] = TIN_MAKERULE( neon_astparser_rulereference, NULL, TINPREC_NONE );
    rules[NEON_TOK_KWFUNCTION] = TIN_MAKERULE(neon_astparser_rulefunction, NULL, TINPREC_NONE);
    rules[NEON_TOK_SEMICOLON] = TIN_MAKERULE(neon_astparser_rulenothing, NULL, TINPREC_NONE);
}


const char* neon_astparser_token2name(int t)
{
    switch(t)
    {
        case NEON_TOK_NEWLINE: return "NEON_TOK_NEWLINE";
        case NEON_TOK_PARENOPEN: return "NEON_TOK_PARENOPEN";
        case NEON_TOK_PARENCLOSE: return "NEON_TOK_PARENCLOSE";
        case NEON_TOK_BRACEOPEN: return "NEON_TOK_BRACEOPEN";
        case NEON_TOK_BRACECLOSE: return "NEON_TOK_BRACECLOSE";
        case NEON_TOK_BRACKETOPEN: return "NEON_TOK_BRACKETOPEN";
        case NEON_TOK_BRACKETCLOSE: return "NEON_TOK_BRACKETCLOSE";
        case NEON_TOK_COMMA: return "NEON_TOK_COMMA";
        case NEON_TOK_SEMICOLON: return "NEON_TOK_SEMICOLON";
        case NEON_TOK_COLON: return "NEON_TOK_COLON";
        case NEON_TOK_ASSIGNEQUAL: return "NEON_TOK_ASSIGNEQUAL";
        case NEON_TOK_BAR: return "NEON_TOK_BAR";
        case NEON_TOK_DOUBLEBAR: return "NEON_TOK_DOUBLEBAR";
        case NEON_TOK_AMPERSANDEQUAL: return "NEON_TOK_AMPERSANDEQUAL";
        case NEON_TOK_AMPERSAND: return "NEON_TOK_AMPERSAND";
        case NEON_TOK_DOUBLEAMPERSAND: return "NEON_TOK_DOUBLEAMPERSAND";
        case NEON_TOK_BANG: return "NEON_TOK_BANG";
        case NEON_TOK_BANGEQUAL: return "NEON_TOK_BANGEQUAL";
        case NEON_TOK_ASSIGN: return "NEON_TOK_ASSIGN";
        case NEON_TOK_EQUAL: return "NEON_TOK_EQUAL";
        case NEON_TOK_GREATERTHAN: return "NEON_TOK_GREATERTHAN";
        case NEON_TOK_GREATEREQUAL: return "NEON_TOK_GREATEREQUAL";
        case NEON_TOK_SHIFTRIGHT: return "NEON_TOK_SHIFTRIGHT";
        case NEON_TOK_LESSTHAN: return "NEON_TOK_LESSTHAN";
        case NEON_TOK_LESSEQUAL: return "NEON_TOK_LESSEQUAL";
        case NEON_TOK_SHIFTLEFT: return "NEON_TOK_SHIFTLEFT";
        case NEON_TOK_PLUS: return "NEON_TOK_PLUS";
        case NEON_TOK_PLUSEQUAL: return "NEON_TOK_PLUSEQUAL";
        case NEON_TOK_DOUBLEPLUS: return "NEON_TOK_DOUBLEPLUS";
        case NEON_TOK_MINUS: return "NEON_TOK_MINUS";
        case NEON_TOK_MINUSEQUAL: return "NEON_TOK_MINUSEQUAL";
        case NEON_TOK_DOUBLEMINUS: return "NEON_TOK_DOUBLEMINUS";
        case NEON_TOK_STAR: return "NEON_TOK_STAR";
        case NEON_TOK_STAREQUAL: return "NEON_TOK_STAREQUAL";
        case NEON_TOK_DOUBLESTAR: return "NEON_TOK_DOUBLESTAR";
        case NEON_TOK_SLASH: return "NEON_TOK_SLASH";
        case NEON_TOK_SLASHEQUAL: return "NEON_TOK_SLASHEQUAL";
        case NEON_TOK_QUESTION: return "NEON_TOK_QUESTION";
        case NEON_TOK_DOUBLEQUESTION: return "NEON_TOK_DOUBLEQUESTION";
        case NEON_TOK_PERCENT: return "NEON_TOK_PERCENT";
        case NEON_TOK_PERCENTEQUAL: return "NEON_TOK_PERCENTEQUAL";
        case NEON_TOK_ARROW: return "NEON_TOK_ARROW";
        case NEON_TOK_SMALLARROW: return "NEON_TOK_SMALLARROW";
        case NEON_TOK_TILDE: return "NEON_TOK_TILDE";
        case NEON_TOK_CARET: return "NEON_TOK_CARET";
        case NEON_TOK_CARETEQUAL: return "NEON_TOK_CARETEQUAL";
        case NEON_TOK_DOT: return "NEON_TOK_DOT";
        case NEON_TOK_DOUBLEDOT: return "NEON_TOK_DOUBLEDOT";
        case NEON_TOK_TRIPLEDOT: return "NEON_TOK_TRIPLEDOT";
        case NEON_TOK_SHARP: return "NEON_TOK_SHARP";
        case NEON_TOK_SHARPEQUAL: return "NEON_TOK_SHARPEQUAL";
        case NEON_TOK_IDENT: return "NEON_TOK_IDENT";
        case NEON_TOK_STRING: return "NEON_TOK_STRING";
        case NEON_TOK_STRINTERPOL: return "NEON_TOK_STRINTERPOL";
        case NEON_TOK_NUMBER: return "NEON_TOK_NUMBER";
        case NEON_TOK_KWCLASS: return "NEON_TOK_KWCLASS";
        case NEON_TOK_KWELSE: return "NEON_TOK_KWELSE";
        case NEON_TOK_KWFALSE: return "NEON_TOK_KWFALSE";
        case NEON_TOK_KWFOR: return "NEON_TOK_KWFOR";
        case NEON_TOK_KWFUNCTION: return "NEON_TOK_KWFUNCTION";
        case NEON_TOK_KWIF: return "NEON_TOK_KWIF";
        case NEON_TOK_KWNULL: return "NEON_TOK_KWNULL";
        case NEON_TOK_KWRETURN: return "NEON_TOK_KWRETURN";
        case NEON_TOK_KWSUPER: return "NEON_TOK_KWSUPER";
        case NEON_TOK_KWTHIS: return "NEON_TOK_KWTHIS";
        case NEON_TOK_KWTRUE: return "NEON_TOK_KWTRUE";
        case NEON_TOK_KWVAR: return "NEON_TOK_KWVAR";
        case NEON_TOK_KWWHILE: return "NEON_TOK_KWWHILE";
        case NEON_TOK_KWCONTINUE: return "NEON_TOK_KWCONTINUE";
        case NEON_TOK_KWBREAK: return "NEON_TOK_KWBREAK";
        case NEON_TOK_KWNEW: return "NEON_TOK_KWNEW";
        case NEON_TOK_KWEXPORT: return "NEON_TOK_KWEXPORT";
        case NEON_TOK_KWIS: return "NEON_TOK_KWIS";
        case NEON_TOK_KWSTATIC: return "NEON_TOK_KWSTATIC";
        case NEON_TOK_KWOPERATOR: return "NEON_TOK_KWOPERATOR";
        case NEON_TOK_KWGET: return "NEON_TOK_KWGET";
        case NEON_TOK_KWSET: return "NEON_TOK_KWSET";
        case NEON_TOK_KWIN: return "NEON_TOK_KWIN";
        case NEON_TOK_KWCONST: return "NEON_TOK_KWCONST";
        case NEON_TOK_KWREF: return "NEON_TOK_KWREF";
        case NEON_TOK_ERROR: return "NEON_TOK_ERROR";
        case NEON_TOK_EOF: return "NEON_TOK_EOF";
        default:
            break;
    }
    return "?unknown?";
}


static void neon_astparser_initcompiler(NeonAstParser* prs, NeonAstCompiler* compiler)
{
    compiler->scope_depth = 0;
    compiler->function = NULL;
    compiler->enclosing = (struct NeonAstCompiler*)prs->compiler;

    prs->compiler = compiler;
}

static void neon_astparser_endcompiler(NeonAstParser* prs, NeonAstCompiler* compiler)
{
    prs->compiler = (NeonAstCompiler*)compiler->enclosing;
}

static void neon_astparser_beginscope(NeonAstParser* prs)
{
    prs->compiler->scope_depth++;
}

static void neon_astparser_endscope(NeonAstParser* prs)
{
    prs->compiler->scope_depth--;
}

static NeonAstParseRule* neon_astparser_getrule(NeonAstTokType type)
{
    return &rules[type];
}

static inline bool prs_is_at_end(NeonAstParser* prs)
{
    return prs->current.type == NEON_TOK_EOF;
}

void neon_astparser_init(NeonState* state, NeonAstParser* prs)
{
    if(!didsetuprules)
    {
        didsetuprules = true;
        neon_astparser_setuprules();
    }
    prs->state = state;
    prs->haderror = false;
    prs->panic_mode = false;
}

void neon_astparser_destroy(NeonAstParser* prs)
{
    (void)prs;
}

static void neon_astparser_raisestring(NeonAstParser* prs, NeonAstToken* token, const char* message)
{
    (void)token;
    if(prs->panic_mode)
    {
        return;
    }
    neon_state_raiseerror(prs->state, COMPILE_ERROR, message);
    prs->haderror = true;
    neon_astparser_sync(prs);
}

static void neon_astparser_raiseat(NeonAstParser* prs, NeonAstToken* token, const char* fmt, va_list args)
{
    neon_astparser_raisestring(prs, token, neon_vformat_error(prs->state, token->line, fmt, args)->data);
}

static void neon_astparser_raiseatcurrent(NeonAstParser* prs, const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    neon_astparser_raiseat(prs, &prs->current, fmt, args);
    va_end(args);
}

static void neon_astparser_raiseerror(NeonAstParser* prs, const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    neon_astparser_raiseat(prs, &prs->previous, fmt, args);
    va_end(args);
}

static void neon_astparser_advance(NeonAstParser* prs)
{
    prs->prevprev = prs->previous;
    prs->previous = prs->current;
    while(true)
    {
        prs->current = neon_astlex_scantoken(prs->state->scanner);
        if(prs->current.type != NEON_TOK_ERROR)
        {
            break;
        }
        neon_astparser_raisestring(prs, &prs->current, prs->current.start);
    }
}

static bool neon_astparser_check(NeonAstParser* prs, NeonAstTokType type)
{
    return prs->current.type == type;
}

static bool neon_astparser_match(NeonAstParser* prs, NeonAstTokType type)
{
    if(prs->current.type == type)
    {
        neon_astparser_advance(prs);
        return true;
    }
    return false;
}

static bool neon_astparser_matchnewline(NeonAstParser* prs)
{
    while(true)
    {
        if(!neon_astparser_match(prs, NEON_TOK_NEWLINE))
        {
            return false;
        }
    }
    return true;
}

static void neon_astparser_ignorenewlines(NeonAstParser* prs, bool checksemi)
{
    (void)checksemi;
    neon_astparser_matchnewline(prs);
}

static void neon_astparser_consume(NeonAstParser* prs, NeonAstTokType type, const char* onerror)
{
    bool line;
    size_t chlen;
    size_t olen;
    const char* fmt;
    const char* otext;
    NeonString* ts;
    if(prs->current.type == type)
    {
        neon_astparser_advance(prs);
        return;
    }
    //fprintf(stderr, "in neon_astparser_consume: failed?\n");
    line = (prs->previous.type == NEON_TOK_NEWLINE);
    otext = "new line";
    olen = strlen(otext);
    if(!line)
    {
        olen = prs->previous.length;
        otext = prs->previous.start; 
    }
    chlen = strlen(otext);
    if(olen > chlen)
    {
        olen = chlen;
    }
    ts = neon_format_error(prs->state, prs->current.line, "expected %s, got '%.*s'", onerror, olen, otext);
    fmt = ts->data;
    neon_astparser_raisestring(prs, &prs->current,fmt);
}

static NeonAstExpression* neon_astparser_parseblock(NeonAstParser* prs)
{
    NeonAstBlockExpr* statement;
    neon_astparser_beginscope(prs);
    statement = neon_ast_make_blockexpr(prs->state, prs->previous.line);
    while(true)
    {
        neon_astparser_ignorenewlines(prs, true);
        if(neon_astparser_check(prs, NEON_TOK_BRACECLOSE) || neon_astparser_check(prs, NEON_TOK_EOF))
        {
            break;
        }
        neon_astparser_ignorenewlines(prs, true);
        neon_exprlist_push(prs->state, &statement->statements, neon_astparser_parsestatement(prs));
        neon_astparser_ignorenewlines(prs, true);
    }
    neon_astparser_ignorenewlines(prs, true);
    neon_astparser_consume(prs, NEON_TOK_BRACECLOSE, "'}'");
    neon_astparser_ignorenewlines(prs, true);
    neon_astparser_endscope(prs);
    return (NeonAstExpression*)statement;
}

static NeonAstExpression* neon_astparser_parseprecedence(NeonAstParser* prs, NeonAstPrecedence precedence, bool err, bool ignsemi)
{
    bool expisnewline;
    bool gotisnewline;
    bool canassign;
    size_t explen;
    size_t gotlen;
    size_t nllen;
    const char* exptext;
    const char* gottxt;
    const char* nltext;
    NeonAstExpression* expr;
    NeonAstParsePrefixFn prefixrule;
    NeonAstParseInfixFn infixrule;
    NeonAstToken previous;
    previous = prs->previous;
    neon_astparser_ignorenewlines(prs, true);
    neon_astparser_advance(prs);
    prefixrule = neon_astparser_getrule(prs->previous.type)->prefix;
    if(prefixrule == NULL)
    {
        nltext = "new line";
        nllen = strlen(nltext);
        {
            // todo: file start
            expisnewline = ((previous.start != NULL) && (*previous.start == '\n'));
            gotisnewline = ((prs->previous.start != NULL) && (*prs->previous.start == '\n'));
            explen = (expisnewline ? nllen : previous.length);
            exptext = (expisnewline ? nltext : previous.start);
            gotlen = (gotisnewline ? nllen : prs->previous.length);
            gottxt = (gotisnewline ? nltext : prs->previous.start);
            neon_astparser_raiseerror(prs, "expected expression after '%.*s', got '%.*s'", explen, exptext, gotlen, gottxt);
            return NULL;
        }
    }
    canassign = precedence <= TINPREC_ASSIGNMENT;
    expr = prefixrule(prs, canassign);
    neon_astparser_ignorenewlines(prs, ignsemi);
    while(precedence <= neon_astparser_getrule(prs->current.type)->precedence)
    {
        neon_astparser_ignorenewlines(prs, true);
        neon_astparser_advance(prs);
        infixrule = neon_astparser_getrule(prs->previous.type)->infix;
        expr = infixrule(prs, expr, canassign);
    }
    if(err && canassign && neon_astparser_match(prs, NEON_TOK_ASSIGN))
    {
        neon_astparser_raiseerror(prs, "invalid assigment target");
    }
    return expr;
}

static NeonAstExpression* neon_astparser_rulenumber(NeonAstParser* prs, bool canassign)
{
    (void)canassign;
    return (NeonAstExpression*)neon_ast_make_literalexpr(prs->state, prs->previous.line, prs->previous.value);
}

static NeonAstExpression* neon_astparser_parselambda(NeonAstParser* prs, NeonAstFunctionExpr* lambda)
{
    lambda->body = neon_astparser_parsestatement(prs);
    return (NeonAstExpression*)lambda;
}

static inline NeonAstParameter neon_astparser_makeparam(const char* name, size_t length, NeonAstExpression* defexpr)
{
    NeonAstParameter tp;
    tp.name = name;
    tp.length = length;
    tp.defaultexpr = defexpr;
    return tp;
}

static void neon_astparser_parseparameters(NeonAstParser* prs, NeonAstParamList* parameters)
{
    bool haddefault;
    size_t arglength;
    const char* argname;
    NeonAstExpression* defexpr;
    haddefault = false;
    while(!neon_astparser_check(prs, NEON_TOK_PARENCLOSE))
    {
        // Vararg ...
        if(neon_astparser_match(prs, NEON_TOK_TRIPLEDOT))
        {
            neon_paramlist_push(prs->state, parameters, neon_astparser_makeparam("...", 3, NULL));
            return;
        }
        neon_astparser_consume(prs, NEON_TOK_IDENT, "argument name");
        argname = prs->previous.start;
        arglength = prs->previous.length;
        defexpr = NULL;
        if(neon_astparser_match(prs, NEON_TOK_ASSIGN))
        {
            haddefault = true;
            defexpr = neon_astparser_parseexpression(prs, true);
        }
        else if(haddefault)
        {
            neon_astparser_raiseerror(prs, "default arguments must always be in the end of the argument list.");
        }
        neon_paramlist_push(prs->state, parameters, neon_astparser_makeparam(argname, arglength, defexpr));
        if(!neon_astparser_match(prs, NEON_TOK_COMMA))
        {
            break;
        }
    }
}

/*
* this is extremely not working at all.
*/
static NeonAstExpression* neon_astparser_rulegroupingorlambda(NeonAstParser* prs, bool canassign)
{
    bool stop;
    bool haddefault;
    bool hadvararg;
    bool had_array;
    bool hadarrow;
    size_t line;
    size_t firstarglength;
    size_t arglength;
    const char* start;
    const char* argname;
    const char* firstargstart;
    NeonAstExpression* expression;
    NeonAstExpression* defexpr;
    NeonAstScanner* scanner;
    (void)canassign;
    (void)hadarrow;
    (void)had_array;
    hadarrow = false;
    if(neon_astparser_match(prs, NEON_TOK_PARENCLOSE))
    {
        neon_astparser_consume(prs, NEON_TOK_ARROW, "=> after lambda arguments");
        return neon_astparser_parselambda(prs, neon_ast_make_lambdaexpr(prs->state, prs->previous.line));
    }
    start = prs->previous.start;
    line = prs->previous.line;
    if(neon_astparser_match(prs, NEON_TOK_IDENT) || neon_astparser_match(prs, NEON_TOK_TRIPLEDOT))
    {
        NeonState* state = prs->state;
        firstargstart = prs->previous.start;
        firstarglength = prs->previous.length;
        if(neon_astparser_match(prs, NEON_TOK_COMMA) || (neon_astparser_match(prs, NEON_TOK_PARENCLOSE) && neon_astparser_match(prs, NEON_TOK_ARROW)))
        {
            had_array = prs->previous.type == NEON_TOK_ARROW;
            hadvararg= prs->previous.type == NEON_TOK_TRIPLEDOT;
            // This is a lambda
            NeonAstFunctionExpr* lambda = neon_ast_make_lambdaexpr(state, line);
            NeonAstExpression* defvalue = NULL;
            haddefault = neon_astparser_match(prs, NEON_TOK_ASSIGN);
            if(haddefault)
            {
                defvalue = neon_astparser_parseexpression(prs, true);
            }
            neon_paramlist_push(state, &lambda->parameters, neon_astparser_makeparam(firstargstart, firstarglength, defvalue));
            if(!hadvararg && prs->previous.type == NEON_TOK_COMMA)
            {
                do
                {
                    stop = false;
                    if(neon_astparser_match(prs, NEON_TOK_TRIPLEDOT))
                    {
                        stop = true;
                    }
                    else
                    {
                        neon_astparser_consume(prs, NEON_TOK_IDENT, "argument name");
                    }

                    argname = prs->previous.start;
                    arglength = prs->previous.length;
                    defexpr = NULL;
                    if(neon_astparser_match(prs, NEON_TOK_ASSIGN))
                    {
                        defexpr = neon_astparser_parseexpression(prs, true);
                        haddefault = true;
                    }
                    else if(haddefault)
                    {
                        neon_astparser_raiseerror(prs, "default arguments must always be in the end of the argument list.");
                    }
                    neon_paramlist_push(state, &lambda->parameters, neon_astparser_makeparam(argname, arglength, defexpr));
                    if(stop)
                    {
                        break;
                    }
                } while(neon_astparser_match(prs, NEON_TOK_COMMA));
            }
            #if 0
            if(!hadarrow)
            {
                neon_astparser_consume(prs, NEON_TOK_PARENCLOSE, "')' after lambda parameters");
                neon_astparser_consume(prs, NEON_TOK_ARROW, "=> after lambda parameters");
            }
            #endif
            return neon_astparser_parselambda(prs, lambda);
        }
        else
        {
            // Ouch, this was a grouping with a single identifier
            scanner = state->scanner;
            scanner->current = start;
            scanner->line = line;
            prs->current = neon_astlex_scantoken(scanner);
            neon_astparser_advance(prs);
        }
    }
    expression = neon_astparser_parseexpression(prs, true);
    neon_astparser_consume(prs, NEON_TOK_PARENCLOSE, "')' after grouping expression");
    return expression;
}

static NeonAstExpression* neon_astparser_rulecall(NeonAstParser* prs, NeonAstExpression* prev, bool canassign)
{
    int pplen;
    /*
    int prevlen;
    int currlen;
    */
    const char* ppstr;
    /*
    const char* prevstr;
    const char* currstr;
    */
    NeonString* ts;
    NeonAstExpression* argexpr;
    NeonAstVarExpr* varex;
    NeonAstCallExpr* callexpr;
    (void)canassign;
    ts = NULL;
    callexpr = neon_ast_make_callexpr(prs->state, prs->previous.line, prev, ts);

    if(ts == NULL)
    {
        pplen = prs->prevprev.length;
        ppstr = prs->prevprev.start;
        /*
        prevlen = prs->previous.length;
        prevstr = prs->previous.start;
        currlen = prs->current.length;
        currstr = prs->current.start;
        fprintf(stderr, "call name: prevprev: '%.*s' previous: '%.*s' current: '%.*s'\n", pplen, ppstr, prevlen, prevstr, currlen, currstr);
        */
        ts = neon_string_copy(prs->state, ppstr, pplen);
    }
    while(!neon_astparser_check(prs, NEON_TOK_PARENCLOSE))
    {
        argexpr = neon_astparser_parseexpression(prs, true);

        neon_exprlist_push(prs->state, &callexpr->args, argexpr);
        if(!neon_astparser_match(prs, NEON_TOK_COMMA))
        {
            break;
        }
        if(argexpr->type == TINEXPR_VAREXPR)
        {
            varex = (NeonAstVarExpr*)argexpr;
            // Vararg ...
            if(varex->length == 3 && memcmp(varex->name, "...", 3) == 0)
            {
                break;
            }
        }
    }
    if(callexpr->args.count > 255)
    {
        neon_astparser_raiseerror(prs, "function cannot have more than 255 arguments, got %i", (int)callexpr->args.count);
    }
    neon_astparser_consume(prs, NEON_TOK_PARENCLOSE, "')' after arguments");
    callexpr->name = ts;
    return (NeonAstExpression*)callexpr;
}

static NeonAstExpression* neon_astparser_ruleunary(NeonAstParser* prs, bool canassign)
{
    (void)canassign;
    size_t line;
    NeonAstExpression* unexpr;
    NeonAstTokType op;
    op = prs->previous.type;
    line = prs->previous.line;
    unexpr = neon_astparser_parseprecedence(prs, TINPREC_UNARY, true, true);
    return (NeonAstExpression*)neon_ast_make_unaryexpr(prs->state, line, unexpr, op);
}

static NeonAstExpression* neon_astparser_rulebinary(NeonAstParser* prs, NeonAstExpression* prev, bool canassign)
{
    (void)canassign;
    bool invert;
    size_t line;
    NeonAstParseRule* rule;
    NeonAstExpression* expression;
    NeonAstTokType op;
    invert = prs->previous.type == NEON_TOK_BANG;
    if(invert)
    {
        neon_astparser_consume(prs, NEON_TOK_KWIS, "'is' after '!'");
    }
    op = prs->previous.type;
    line = prs->previous.line;
    rule = neon_astparser_getrule(op);
    expression = neon_astparser_parseprecedence(prs, (NeonAstPrecedence)(rule->precedence + 1), true, true);
    expression = (NeonAstExpression*)neon_ast_make_binaryexpr(prs->state, line, prev, expression, op);
    if(invert)
    {
        expression = (NeonAstExpression*)neon_ast_make_unaryexpr(prs->state, line, expression, NEON_TOK_BANG);
    }
    return expression;
}

static NeonAstExpression* neon_astparser_ruleand(NeonAstParser* prs, NeonAstExpression* prev, bool canassign)
{
    (void)canassign;
    size_t line;
    NeonAstTokType op;
    op = prs->previous.type;
    line = prs->previous.line;
    return (NeonAstExpression*)neon_ast_make_binaryexpr(prs->state, line, prev, neon_astparser_parseprecedence(prs, TINPREC_AND, true, true), op);
}

static NeonAstExpression* neon_astparser_ruleor(NeonAstParser* prs, NeonAstExpression* prev, bool canassign)
{
    (void)canassign;
    size_t line;
    NeonAstTokType op;
    op = prs->previous.type;
    line = prs->previous.line;
    return (NeonAstExpression*)neon_ast_make_binaryexpr(prs->state, line, prev, neon_astparser_parseprecedence(prs, TINPREC_OR, true, true), op);
}

static NeonAstExpression* neon_astparser_rulenull_filter(NeonAstParser* prs, NeonAstExpression* prev, bool canassign)
{
    (void)canassign;
    size_t line;
    NeonAstTokType op;
    op = prs->previous.type;
    line = prs->previous.line;
    return (NeonAstExpression*)neon_ast_make_binaryexpr(prs->state, line, prev, neon_astparser_parseprecedence(prs, TINPREC_NULL, true, true), op);
}

static NeonAstTokType neon_astparser_convertcompoundop(NeonAstTokType op)
{
    switch(op)
    {
        case NEON_TOK_PLUSEQUAL:
            {
                return NEON_TOK_PLUS;
            }
            break;
        case NEON_TOK_MINUSEQUAL:
            {
                return NEON_TOK_MINUS;
            }
            break;
        case NEON_TOK_STAREQUAL:
            {
                return NEON_TOK_STAR;
            }
            break;
        case NEON_TOK_SLASHEQUAL:
            {
                return NEON_TOK_SLASH;
            }
            break;
        case NEON_TOK_SHARPEQUAL:
            {
                return NEON_TOK_SHARP;
            }
            break;
        case NEON_TOK_PERCENTEQUAL:
            {
                return NEON_TOK_PERCENT;
            }
            break;
        case NEON_TOK_CARETEQUAL:
            {
                return NEON_TOK_CARET;
            }
            break;
        case NEON_TOK_ASSIGNEQUAL:
            {
                return NEON_TOK_BAR;
            }
            break;
        case NEON_TOK_AMPERSANDEQUAL:
            {
                return NEON_TOK_AMPERSAND;
            }
            break;
        case NEON_TOK_DOUBLEPLUS:
            {
                return NEON_TOK_PLUS;
            }
            break;
        case NEON_TOK_DOUBLEMINUS:
            {
                return NEON_TOK_MINUS;
            }
            break;
        default:
            {
                assert(!"missing or invalid instruction for operator in convertcompound");
            }
            break;
    }
    return (NeonAstTokType)-1;
}

static NeonAstExpression* neon_astparser_rulecompound(NeonAstParser* prs, NeonAstExpression* prev, bool canassign)
{
    (void)canassign;
    size_t line;
    NeonAstBinaryExpr* binary;
    NeonAstExpression* expression;
    NeonAstParseRule* rule;
    NeonAstTokType op;
    op = prs->previous.type;
    line = prs->previous.line;
    rule = neon_astparser_getrule(op);
    if(op == NEON_TOK_DOUBLEPLUS || op == NEON_TOK_DOUBLEMINUS)
    {
        expression = (NeonAstExpression*)neon_ast_make_literalexpr(prs->state, line, neon_value_makefixednumber(prs->state, 1));
    }
    else
    {
        expression = neon_astparser_parseprecedence(prs, (NeonAstPrecedence)(rule->precedence + 1), true, true);
    }
    binary = neon_ast_make_binaryexpr(prs->state, line, prev, expression, neon_astparser_convertcompoundop(op));
    // To make sure we don't free it twice
    binary->ignore_left = true;
    return (NeonAstExpression*)neon_ast_make_assignexpr(prs->state, line, prev, (NeonAstExpression*)binary);
}

static NeonAstExpression* neon_astparser_ruleliteral(NeonAstParser* prs, bool canassign)
{
    (void)canassign;
    size_t line;
    line = prs->previous.line;
    switch(prs->previous.type)
    {
        case NEON_TOK_KWTRUE:
            {
                return (NeonAstExpression*)neon_ast_make_literalexpr(prs->state, line, neon_value_makebool(prs->state, true));
            }
            break;
        case NEON_TOK_KWFALSE:
            {
                return (NeonAstExpression*)neon_ast_make_literalexpr(prs->state, line, neon_value_makebool(prs->state, false));
            }
            break;
        case NEON_TOK_KWNULL:
            {
                return (NeonAstExpression*)neon_ast_make_literalexpr(prs->state, line, neon_value_makenull(prs->state));
            }
            break;
        default:
            {
                assert(!"missing or invalid instruction for ruleliteral");
            }
            break;
    }
    return NULL;
}

static NeonAstExpression* neon_astparser_rulestring(NeonAstParser* prs, bool canassign)
{
    (void)canassign;
    NeonAstExpression* expression;
    expression = (NeonAstExpression*)neon_ast_make_literalexpr(prs->state, prs->previous.line, prs->previous.value);
    if(neon_astparser_match(prs, NEON_TOK_BRACKETOPEN))
    {
        return neon_astparser_rulesubscript(prs, expression, canassign);
    }
    return expression;
}

static NeonAstExpression* neon_astparser_ruleinterpolation(NeonAstParser* prs, bool canassign)
{
    NeonAstStrInterExpr* expression;
    (void)canassign;
    expression = neon_ast_make_strinterpolexpr(prs->state, prs->previous.line);
    do
    {
        if(neon_string_getlength(neon_value_asstring(prs->previous.value)) > 0)
        {
            neon_exprlist_push(
            prs->state, &expression->expressions,
            (NeonAstExpression*)neon_ast_make_literalexpr(prs->state, prs->previous.line, prs->previous.value));
        }
        neon_exprlist_push(prs->state, &expression->expressions, neon_astparser_parseexpression(prs, true));
    } while(neon_astparser_match(prs, NEON_TOK_STRINTERPOL));
    neon_astparser_consume(prs, NEON_TOK_STRING, "end of interpolation");
    if(neon_string_getlength(neon_value_asstring(prs->previous.value)) > 0)
    {
        neon_exprlist_push(
        prs->state, &expression->expressions,
        (NeonAstExpression*)neon_ast_make_literalexpr(prs->state, prs->previous.line, prs->previous.value));
    }
    if(neon_astparser_match(prs, NEON_TOK_BRACKETOPEN))
    {
        return neon_astparser_rulesubscript(prs, (NeonAstExpression*)expression, canassign);
    }
    return (NeonAstExpression*)expression;
}

static NeonAstExpression* neon_astparser_ruleobject(NeonAstParser* prs, bool canassign)
{
    NeonString* ts;
    NeonValue tv;
    NeonAstExpression* expr;
    NeonAstObjectExpr* object;
    (void)canassign;
    (void)ts;
    object = neon_ast_make_objectexpr(prs->state, prs->previous.line);
    neon_astparser_ignorenewlines(prs, true);
    while(!neon_astparser_check(prs, NEON_TOK_BRACECLOSE))
    {
        neon_astparser_ignorenewlines(prs, true);
        if(neon_astparser_check(prs, NEON_TOK_IDENT))
        {
            neon_astparser_raiseerror(prs, "cannot use bare names (for now)");
            //neon_astparser_consume(prs, NEON_TOK_IDENT, "key string after '{'");
            
            //neon_vallist_push(prs->state, &object->keys, neon_value_fromobject(neon_string_copy(prs->state, prs->previous.start, prs->previous.length)));
            expr = neon_astparser_parseexpression(prs, true);
            neon_exprlist_push(prs->state, &object->keys, expr);

        }
        else if(neon_astparser_check(prs, NEON_TOK_STRING))
        {
            expr = neon_astparser_parseexpression(prs, true);
            tv = prs->previous.value;
            ts = neon_value_asstring(tv);
            //neon_vallist_push(prs->state, &object->keys, neon_value_fromobject(neon_string_copy(prs->state, ts->data, neon_string_getlength(ts))));
            //neon_vallist_push(prs->state, &object->keys, tv);
            neon_exprlist_push(prs->state, &object->keys, expr);

            //neon_ast_destroyexpression(prs->state, expr);
        }
        
        else
        {
            neon_astparser_raiseerror(prs, "expect identifier or string as object key");
        }
    
        neon_astparser_ignorenewlines(prs, true);
        neon_astparser_consume(prs, NEON_TOK_COLON, "':' after key string");
        neon_astparser_ignorenewlines(prs, true);
        neon_exprlist_push(prs->state, &object->values, neon_astparser_parseexpression(prs, true));
        if(!neon_astparser_match(prs, NEON_TOK_COMMA))
        {
            break;
        }
    }
    neon_astparser_ignorenewlines(prs, true);
    neon_astparser_consume(prs, NEON_TOK_BRACECLOSE, "'}' after object");
    return (NeonAstExpression*)object;
}

static NeonAstExpression* neon_astparser_rulevarexprbase(NeonAstParser* prs, bool canassign, bool isnew)
{
    (void)canassign;
    bool hadargs;
    NeonString* ts;
    NeonAstCallExpr* callex;
    NeonAstExpression* expression;
    expression = (NeonAstExpression*)neon_ast_make_varexpr(prs->state, prs->previous.line, prs->previous.start, prs->previous.length);
    if(isnew)
    {
        hadargs = neon_astparser_check(prs, NEON_TOK_PARENOPEN);
        callex = NULL;
        if(hadargs)
        {
            neon_astparser_advance(prs);
            callex = (NeonAstCallExpr*)neon_astparser_rulecall(prs, expression, false);
        }
        if(neon_astparser_match(prs, NEON_TOK_BRACEOPEN))
        {
            if(callex == NULL)
            {
                ts =  neon_string_copy(prs->state, prs->previous.start, prs->previous.length);
                callex = neon_ast_make_callexpr(prs->state, expression->line, expression, ts);
            }
            callex->init = neon_astparser_ruleobject(prs, false);
        }
        else if(!hadargs)
        {
            neon_astparser_raiseatcurrent(prs, "expected %s, got '%.*s'", "argument list for instance creation",
                             prs->previous.length, prs->previous.start);
        }
        return (NeonAstExpression*)callex;
    }
    if(neon_astparser_match(prs, NEON_TOK_BRACKETOPEN))
    {
        return neon_astparser_rulesubscript(prs, expression, canassign);
    }
    if(canassign && neon_astparser_match(prs, NEON_TOK_ASSIGN))
    {
        return (NeonAstExpression*)neon_ast_make_assignexpr(prs->state, prs->previous.line, expression,
                                                            neon_astparser_parseexpression(prs, true));
    }
    return expression;
}

static NeonAstExpression* neon_astparser_rulevarexpr(NeonAstParser* prs, bool canassign)
{
    return neon_astparser_rulevarexprbase(prs, canassign, false);
}

static NeonAstExpression* neon_astparser_rulenewexpr(NeonAstParser* prs, bool canassign)
{
    (void)canassign;
    neon_astparser_consume(prs, NEON_TOK_IDENT, "class name after 'new'");
    return neon_astparser_rulevarexprbase(prs, false, true);
}

static NeonAstExpression* neon_astparser_ruledot(NeonAstParser* prs, NeonAstExpression* previous, bool canassign)
{
    (void)canassign;
    bool ignored;
    size_t line;
    size_t length;
    const char* name;
    NeonAstExpression* expression;
    line = prs->previous.line;
    ignored = prs->previous.type == NEON_TOK_SMALLARROW;
    if(!(neon_astparser_match(prs, NEON_TOK_KWCLASS) || neon_astparser_match(prs, NEON_TOK_KWSUPER)))
    {// class and super are allowed field names
        neon_astparser_consume(prs, NEON_TOK_IDENT, ignored ? "propety name after '->'" : "property name after '.'");
    }
    name = prs->previous.start;
    length = prs->previous.length;
    if(!ignored && canassign && neon_astparser_match(prs, NEON_TOK_ASSIGN))
    {
        return (NeonAstExpression*)neon_ast_make_setexpr(prs->state, line, previous, name, length, neon_astparser_parseexpression(prs, true));
    }
    expression = (NeonAstExpression*)neon_ast_make_getexpr(prs->state, line, previous, name, length, false, ignored);
    if(!ignored && neon_astparser_match(prs, NEON_TOK_BRACKETOPEN))
    {
        return neon_astparser_rulesubscript(prs, expression, canassign);
    }
    return expression;
}

static NeonAstExpression* neon_astparser_rulerange(NeonAstParser* prs, NeonAstExpression* previous, bool canassign)
{
    (void)canassign;
    size_t line;
    line = prs->previous.line;
    return (NeonAstExpression*)neon_ast_make_rangeexpr(prs->state, line, previous, neon_astparser_parseexpression(prs, true));
}

static NeonAstExpression* neon_astparser_ruleternary(NeonAstParser* prs, NeonAstExpression* previous, bool canassign)
{
    (void)canassign;
    bool ignored;
    size_t line;
    NeonAstExpression* ifbranch;
    NeonAstExpression* elsebranch;
    line = prs->previous.line;
    if(neon_astparser_match(prs, NEON_TOK_DOT) || neon_astparser_match(prs, NEON_TOK_SMALLARROW))
    {
        ignored = prs->previous.type == NEON_TOK_SMALLARROW;
        neon_astparser_consume(prs, NEON_TOK_IDENT, ignored ? "property name after '->'" : "property name after '.'");
        return (NeonAstExpression*)neon_ast_make_getexpr(prs->state, line, previous, prs->previous.start,
                                                         prs->previous.length, true, ignored);
    }
    ifbranch = neon_astparser_parseexpression(prs, true);
    neon_astparser_consume(prs, NEON_TOK_COLON, "':' after expression");
    elsebranch = neon_astparser_parseexpression(prs, true);
    return (NeonAstExpression*)neon_ast_make_ternaryexpr(prs->state, line, previous, ifbranch, elsebranch);
}

static NeonAstExpression* neon_astparser_rulearray(NeonAstParser* prs, bool canassign)
{
    (void)canassign;
    NeonAstExpression* expr;
    NeonAstArrayExpr* array;
    array = neon_ast_make_arrayexpr(prs->state, prs->previous.line);
    neon_astparser_ignorenewlines(prs, true);
    while(!neon_astparser_check(prs, NEON_TOK_BRACKETCLOSE))
    {
        expr = NULL;
        neon_astparser_ignorenewlines(prs, true);
        #if 1
            expr = neon_astparser_parseexpression(prs, true);
        #else
            if(neon_astparser_check(prs, NEON_TOK_COMMA))
            {
                //neon_astparser_rulenull_filter(NeonAstParser *prs, NeonAstExpression *prev, _Bool canassign)
                expr = neon_astparser_rulenull_filter(prs, NULL, false);
            }
            else
            {
                expr = neon_astparser_parseexpression(prs, true);
            }
        #endif
        neon_exprlist_push(prs->state, &array->values, expr);
        if(!neon_astparser_match(prs, NEON_TOK_COMMA))
        {
            break;
        }
        neon_astparser_ignorenewlines(prs, true);
    }
    neon_astparser_ignorenewlines(prs, true);
    neon_astparser_consume(prs, NEON_TOK_BRACKETCLOSE, "']' after array");
    if(neon_astparser_match(prs, NEON_TOK_BRACKETOPEN))
    {
        return neon_astparser_rulesubscript(prs, (NeonAstExpression*)array, canassign);
    }
    return (NeonAstExpression*)array;
}

static NeonAstExpression* neon_astparser_rulesubscript(NeonAstParser* prs, NeonAstExpression* previous, bool canassign)
{
    size_t line;
    NeonAstExpression* index;
    NeonAstExpression* expression;
    line = prs->previous.line;
    index = neon_astparser_parseexpression(prs, true);
    neon_astparser_consume(prs, NEON_TOK_BRACKETCLOSE, "']' after subscript");
    expression = (NeonAstExpression*)neon_ast_make_subscriptexpr(prs->state, line, previous, index);
    if(neon_astparser_match(prs, NEON_TOK_BRACKETOPEN))
    {
        return neon_astparser_rulesubscript(prs, expression, canassign);
    }
    else if(canassign && neon_astparser_match(prs, NEON_TOK_ASSIGN))
    {
        return (NeonAstExpression*)neon_ast_make_assignexpr(prs->state, prs->previous.line, expression, neon_astparser_parseexpression(prs, true));
    }
    return expression;
}

static NeonAstExpression* neon_astparser_rulethis(NeonAstParser* prs, bool canassign)
{
    NeonAstExpression* expression;
    expression = (NeonAstExpression*)neon_ast_make_thisexpr(prs->state, prs->previous.line);
    if(neon_astparser_match(prs, NEON_TOK_BRACKETOPEN))
    {
        return neon_astparser_rulesubscript(prs, expression, canassign);
    }
    return expression;
}

static NeonAstExpression* neon_astparser_rulesuper(NeonAstParser* prs, bool canassign)
{
    (void)canassign;
    bool ignoring;
    size_t line;
    NeonAstExpression* expression;
    line = prs->previous.line;
    if(!(neon_astparser_match(prs, NEON_TOK_DOT) || neon_astparser_match(prs, NEON_TOK_SMALLARROW)))
    {
        expression = (NeonAstExpression*)neon_ast_make_superexpr(
        prs->state, line, neon_string_copy(prs->state, TIN_VALUE_CTORNAME, strlen(TIN_VALUE_CTORNAME)), false);
        neon_astparser_consume(prs, NEON_TOK_PARENOPEN, "'(' after 'super'");
        return neon_astparser_rulecall(prs, expression, false);
    }
    ignoring = prs->previous.type == NEON_TOK_SMALLARROW;
    neon_astparser_consume(prs, NEON_TOK_IDENT, ignoring ? "super method name after '->'" : "super method name after '.'");
    expression = (NeonAstExpression*)neon_ast_make_superexpr(
    prs->state, line, neon_string_copy(prs->state, prs->previous.start, prs->previous.length), ignoring);
    if(neon_astparser_match(prs, NEON_TOK_PARENOPEN))
    {
        return neon_astparser_rulecall(prs, expression, false);
    }
    return expression;
}

static NeonAstExpression *neon_astparser_rulenothing(NeonAstParser *prs, bool canassign)
{
    (void)prs;
    (void)canassign;
    return NULL;
}

static NeonAstExpression* neon_astparser_rulereference(NeonAstParser* prs, bool canassign)
{
    size_t line;
    NeonAstRefExpr* expression;
    (void)canassign;
    line = prs->previous.line;
    neon_astparser_ignorenewlines(prs, true);
    expression = neon_ast_make_referenceexpr(prs->state, line, neon_astparser_parseprecedence(prs, TINPREC_CALL, false, true));
    if(neon_astparser_match(prs, NEON_TOK_ASSIGN))
    {
        return (NeonAstExpression*)neon_ast_make_assignexpr(prs->state, line, (NeonAstExpression*)expression, neon_astparser_parseexpression(prs, true));
    }
    return (NeonAstExpression*)expression;
}



static NeonAstExpression* neon_astparser_parsestatement(NeonAstParser* prs)
{
    NeonAstExpression* expression;
    neon_astparser_ignorenewlines(prs, true);
    if(setjmp(prs_jmpbuffer))
    {
        return NULL;
    }
    if(neon_astparser_match(prs, NEON_TOK_KWVAR) || neon_astparser_match(prs, NEON_TOK_KWCONST))
    {
        return neon_astparser_parsevar_declaration(prs, true);
    }
    else if(neon_astparser_match(prs, NEON_TOK_KWIF))
    {
        return neon_astparser_parseif(prs);
    }
    else if(neon_astparser_match(prs, NEON_TOK_KWFOR))
    {
        return neon_astparser_parsefor(prs);
    }
    else if(neon_astparser_match(prs, NEON_TOK_KWWHILE))
    {
        return neon_astparser_parsewhile(prs);
    }
    else if(neon_astparser_match(prs, NEON_TOK_KWCONTINUE))
    {
        return (NeonAstExpression*)neon_ast_make_continueexpr(prs->state, prs->previous.line);
    }
    else if(neon_astparser_match(prs, NEON_TOK_KWBREAK))
    {
        return (NeonAstExpression*)neon_ast_make_breakexpr(prs->state, prs->previous.line);
    }
    else if(neon_astparser_match(prs, NEON_TOK_KWFUNCTION) || neon_astparser_match(prs, NEON_TOK_KWEXPORT))
    {
        return neon_astparser_rulefunction(prs, false);
    }
    else if(neon_astparser_match(prs, NEON_TOK_KWRETURN))
    {
        return neon_astparser_parsereturn(prs);
    }
    else if(neon_astparser_match(prs, NEON_TOK_BRACEOPEN))
    {
        return neon_astparser_parseblock(prs);
    }
    expression = neon_astparser_parseexpression(prs, true);
    if(expression == NULL)
    {
        return NULL;
    }
    return (NeonAstExpression*)neon_ast_make_exprstmt(prs->state, prs->previous.line, expression);
}

static NeonAstExpression* neon_astparser_parseexpression(NeonAstParser* prs, bool ignsemi)
{
    neon_astparser_ignorenewlines(prs, ignsemi);
    return neon_astparser_parseprecedence(prs, TINPREC_ASSIGNMENT, true, ignsemi);
}

static NeonAstExpression* neon_astparser_parsevar_declaration(NeonAstParser* prs, bool ignsemi)
{
    bool constant;
    size_t line;
    size_t length;
    const char* name;
    NeonAstExpression* init;
    constant = prs->previous.type == NEON_TOK_KWCONST;
    line = prs->previous.line;
    neon_astparser_consume(prs, NEON_TOK_IDENT, "variable name");
    name = prs->previous.start;
    length = prs->previous.length;
    init = NULL;
    if(neon_astparser_match(prs, NEON_TOK_ASSIGN))
    {
        init = neon_astparser_parseexpression(prs, ignsemi);
    }
    return (NeonAstExpression*)neon_ast_make_assignvarexpr(prs->state, line, name, length, init, constant);
}

static NeonAstExpression* neon_astparser_parseif(NeonAstParser* prs)
{
    size_t line;
    bool invert;
    bool hadparen;
    NeonAstExpression* condition;
    NeonAstExpression* ifbranch;
    NeonAstExprList* elseifconds;
    NeonAstExprList* elseifbranches;
    NeonAstExpression* elsebranch;
    NeonAstExpression* e;
    line = prs->previous.line;
    invert = neon_astparser_match(prs, NEON_TOK_BANG);
    hadparen = neon_astparser_match(prs, NEON_TOK_PARENOPEN);
    condition = neon_astparser_parseexpression(prs, true);
    if(hadparen)
    {
        neon_astparser_consume(prs, NEON_TOK_PARENCLOSE, "')'");
    }
    if(invert)
    {
        condition = (NeonAstExpression*)neon_ast_make_unaryexpr(prs->state, condition->line, condition, NEON_TOK_BANG);
    }
    neon_astparser_ignorenewlines(prs, true);
    ifbranch = neon_astparser_parsestatement(prs);
    elseifconds = NULL;
    elseifbranches = NULL;
    elsebranch = NULL;
    neon_astparser_ignorenewlines(prs, true);
    while(neon_astparser_match(prs, NEON_TOK_KWELSE))
    {
        // else if
        neon_astparser_ignorenewlines(prs, true);
        if(neon_astparser_match(prs, NEON_TOK_KWIF))
        {
            if(elseifconds == NULL)
            {
                elseifconds = neon_ast_allocexprlist(prs->state);
                elseifbranches = neon_ast_allocate_stmtlist(prs->state);
            }
            invert = neon_astparser_match(prs, NEON_TOK_BANG);
            hadparen = neon_astparser_match(prs, NEON_TOK_PARENOPEN);
            neon_astparser_ignorenewlines(prs, true);
            e = neon_astparser_parseexpression(prs, true);
            if(hadparen)
            {
                neon_astparser_consume(prs, NEON_TOK_PARENCLOSE, "')'");
            }
            neon_astparser_ignorenewlines(prs, true);
            if(invert)
            {
                e = (NeonAstExpression*)neon_ast_make_unaryexpr(prs->state, condition->line, e, NEON_TOK_BANG);
            }
            neon_exprlist_push(prs->state, elseifconds, e);
            neon_exprlist_push(prs->state, elseifbranches, neon_astparser_parsestatement(prs));
            continue;
        }
        // else
        if(elsebranch != NULL)
        {
            neon_astparser_raiseerror(prs, "if-statement can have only one else-branch");
        }
        neon_astparser_ignorenewlines(prs, true);
        elsebranch = neon_astparser_parsestatement(prs);
    }
    return (NeonAstExpression*)neon_ast_make_ifexpr(prs->state, line, condition, ifbranch, elsebranch, elseifconds, elseifbranches);
}

static NeonAstExpression* neon_astparser_parsefor(NeonAstParser* prs)
{
    bool cstyle;
    bool hadparen;
    size_t line;
    NeonAstExpression* condition;
    NeonAstExpression* increment;
    NeonAstExpression* var;
    NeonAstExpression* init;
    line= prs->previous.line;
    hadparen = neon_astparser_match(prs, NEON_TOK_PARENOPEN);
    var = NULL;
    init = NULL;
    if(!neon_astparser_check(prs, NEON_TOK_SEMICOLON))
    {
        if(neon_astparser_match(prs, NEON_TOK_KWVAR))
        {
            var = neon_astparser_parsevar_declaration(prs, false);
        }
        else
        {
            init = neon_astparser_parseexpression(prs, false);
        }
    }
    cstyle = !neon_astparser_match(prs, NEON_TOK_KWIN);
    condition= NULL;
    increment = NULL;
    if(cstyle)
    {
        neon_astparser_consume(prs, NEON_TOK_SEMICOLON, "';'");
        condition = NULL;
    
        if(!neon_astparser_check(prs, NEON_TOK_SEMICOLON))
        {
            condition = neon_astparser_parseexpression(prs, false);
        }
        neon_astparser_consume(prs, NEON_TOK_SEMICOLON, "';'");
        increment = NULL;
        if(!neon_astparser_check(prs, NEON_TOK_PARENCLOSE))
        {
            increment = neon_astparser_parseexpression(prs, false);
        }
    }
    else
    {
        condition = neon_astparser_parseexpression(prs, true);
        if(var == NULL)
        {
            neon_astparser_raiseerror(prs, "for-loops using in-iteration must declare a new variable");
        }
    }
    if(hadparen)
    {
        neon_astparser_consume(prs, NEON_TOK_PARENCLOSE, "')'");
    }
    neon_astparser_ignorenewlines(prs, true);
    return (NeonAstExpression*)neon_ast_make_forexpr(prs->state, line, init, var, condition, increment,
                                                   neon_astparser_parsestatement(prs), cstyle);
}

static NeonAstExpression* neon_astparser_parsewhile(NeonAstParser* prs)
{
    bool hadparen;
    size_t line;
    NeonAstExpression* body;
    line = prs->previous.line;
    hadparen = neon_astparser_match(prs, NEON_TOK_PARENOPEN);
    NeonAstExpression* condition = neon_astparser_parseexpression(prs, true);
    if(hadparen)
    {
        neon_astparser_consume(prs, NEON_TOK_PARENCLOSE, "')'");
    }
    neon_astparser_ignorenewlines(prs, true);
    body = neon_astparser_parsestatement(prs);
    return (NeonAstExpression*)neon_ast_make_whileexpr(prs->state, line, condition, body);
}

static NeonAstExpression* neon_astparser_rulefunction(NeonAstParser* prs, bool canassign)
{
    bool isexport;
    bool islambda;
    size_t line;
    size_t fnamelen;
    const char* fnamestr;
    NeonAstCompiler compiler;
    NeonAstFunctionExpr* function;
    NeonAstFunctionExpr* lambda;
    NeonAstSetExpr* to;
    islambda = canassign;
    isexport = prs->previous.type == NEON_TOK_KWEXPORT;
    fnamestr = "<anonymous>";
    fnamelen = strlen(fnamestr);
    if(isexport)
    {
        neon_astparser_consume(prs, NEON_TOK_KWFUNCTION, "'function' after 'export'");
    }
    line = prs->previous.line;
    if(neon_astparser_check(prs, NEON_TOK_IDENT))
    {
        neon_astparser_consume(prs, NEON_TOK_IDENT, "function name");
        fnamestr = prs->previous.start;
        fnamelen = prs->previous.length;
    }
    if(neon_astparser_match(prs, NEON_TOK_DOT) || islambda)
    //if(neon_astparser_match(prs, NEON_TOK_DOT))
    {
        to = NULL;
        if(neon_astparser_check(prs, NEON_TOK_IDENT))
        {
            neon_astparser_consume(prs, NEON_TOK_IDENT, "function name");
        }
        lambda = neon_ast_make_lambdaexpr(prs->state, line);
        //if(islambda)
        /*
        {
            to = neon_ast_make_setexpr(
                prs->state,
                line,
                (NeonAstExpression*)neon_ast_make_varexpr(prs->state, line, fnamestr, fnamelen),
                prs->previous.start,
                prs->previous.length,
                (NeonAstExpression*)lambda
            );
        }
        */
        neon_astparser_consume(prs, NEON_TOK_PARENOPEN, "'(' after function name");
        neon_astparser_initcompiler(prs, &compiler);
        neon_astparser_beginscope(prs);
        neon_astparser_parseparameters(prs, &lambda->parameters);
        if(lambda->parameters.count > 255)
        {
            neon_astparser_raiseerror(prs, "function cannot have more than 255 arguments, got %i", (int)lambda->parameters.count);
        }
        neon_astparser_consume(prs, NEON_TOK_PARENCLOSE, "')' after function arguments");
        neon_astparser_ignorenewlines(prs, true);
        lambda->body = neon_astparser_parsestatement(prs);
        neon_astparser_endscope(prs);
        neon_astparser_endcompiler(prs, &compiler);
        if(islambda)
        {
            return (NeonAstExpression*)lambda;
        }
        return (NeonAstExpression*)neon_ast_make_exprstmt(prs->state, line, (NeonAstExpression*)to);
    }
    function = neon_ast_make_funcexpr(prs->state, line, fnamestr, fnamelen);
    function->exported = isexport;
    neon_astparser_consume(prs, NEON_TOK_PARENOPEN, "'(' after function name");
    neon_astparser_initcompiler(prs, &compiler);
    neon_astparser_beginscope(prs);
    neon_astparser_parseparameters(prs, &function->parameters);
    if(function->parameters.count > 255)
    {
        neon_astparser_raiseerror(prs, "function cannot have more than 255 arguments, got %i", (int)function->parameters.count);
    }
    neon_astparser_consume(prs, NEON_TOK_PARENCLOSE, "')' after function arguments");
    function->body = neon_astparser_parsestatement(prs);
    neon_astparser_endscope(prs);
    neon_astparser_endcompiler(prs, &compiler);
    return (NeonAstExpression*)function;
}

static NeonAstExpression* neon_astparser_parsereturn(NeonAstParser* prs)
{
    size_t line;
    NeonAstExpression* expression;
    line = prs->previous.line;
    expression = NULL;
    if(!neon_astparser_check(prs, NEON_TOK_NEWLINE) && !neon_astparser_check(prs, NEON_TOK_BRACECLOSE))
    {
        expression = neon_astparser_parseexpression(prs, true);
    }
    return (NeonAstExpression*)neon_ast_make_returnexpr(prs->state, line, expression);
}

static NeonAstExpression* neon_astparser_parsefield(NeonAstParser* prs, NeonString* name, bool isstatic)
{
    size_t line;
    NeonAstExpression* getter;
    NeonAstExpression* setter;
    line = prs->previous.line;
    getter = NULL;
    setter = NULL;
    if(neon_astparser_match(prs, NEON_TOK_ARROW))
    {
        getter = neon_astparser_parsestatement(prs);
    }
    else
    {
        neon_astparser_match(prs, NEON_TOK_BRACEOPEN);// Will be NEON_TOK_BRACEOPEN, otherwise this method won't be called
        neon_astparser_ignorenewlines(prs, true);
        if(neon_astparser_match(prs, NEON_TOK_KWGET))
        {
            neon_astparser_match(prs, NEON_TOK_ARROW);// Ignore it if it's present
            getter = neon_astparser_parsestatement(prs);
        }
        neon_astparser_ignorenewlines(prs, true);
        if(neon_astparser_match(prs, NEON_TOK_KWSET))
        {
            neon_astparser_match(prs, NEON_TOK_ARROW);// Ignore it if it's present
            setter = neon_astparser_parsestatement(prs);
        }
        if(getter == NULL && setter == NULL)
        {
            neon_astparser_raiseerror(prs, "expected declaration of either getter or setter, got none");
        }
        neon_astparser_ignorenewlines(prs, true);
        neon_astparser_consume(prs, NEON_TOK_BRACECLOSE, "'}' after field declaration");
    }
    return (NeonAstExpression*)neon_ast_make_fieldexpr(prs->state, line, name, getter, setter, isstatic);
}

static NeonAstExpression* neon_astparser_parsemethod(NeonAstParser* prs, bool isstatic)
{
    size_t i;
    NeonAstCompiler compiler;
    NeonAstMethodExpr* method;
    NeonString* name;
    if(neon_astparser_match(prs, NEON_TOK_KWSTATIC))
    {
        isstatic = true;
    }
    name = NULL;
    if(neon_astparser_match(prs, NEON_TOK_KWOPERATOR))
    {
        if(isstatic)
        {
            neon_astparser_raiseerror(prs, "operator methods cannot be static or defined in static classes");
        }
        i = 0;
        while(operators[i] != NEON_TOK_EOF)
        {
            if(neon_astparser_match(prs, operators[i]))
            {
                break;
            }
            i++;
        }
        if(prs->previous.type == NEON_TOK_BRACKETOPEN)
        {
            neon_astparser_consume(prs, NEON_TOK_BRACKETCLOSE, "']' after '[' in op method declaration");
            name = neon_string_copy(prs->state, "[]", 2);
        }
        else
        {
            name = neon_string_copy(prs->state, prs->previous.start, prs->previous.length);
        }
    }
    else
    {
        neon_astparser_consume(prs, NEON_TOK_IDENT, "method name");
        name = neon_string_copy(prs->state, prs->previous.start, prs->previous.length);
        if(neon_astparser_check(prs, NEON_TOK_BRACEOPEN) || neon_astparser_check(prs, NEON_TOK_ARROW))
        {
            return neon_astparser_parsefield(prs, name, isstatic);
        }
    }
    method = neon_ast_make_methodexpr(prs->state, prs->previous.line, name, isstatic);
    neon_astparser_initcompiler(prs, &compiler);
    neon_astparser_beginscope(prs);
    neon_astparser_consume(prs, NEON_TOK_PARENOPEN, "'(' after method name");
    neon_astparser_parseparameters(prs, &method->parameters);
    if(method->parameters.count > 255)
    {
        neon_astparser_raiseerror(prs, "function cannot have more than 255 arguments, got %i", (int)method->parameters.count);
    }
    neon_astparser_consume(prs, NEON_TOK_PARENCLOSE, "')' after method arguments");
    method->body = neon_astparser_parsestatement(prs);
    neon_astparser_endscope(prs);
    neon_astparser_endcompiler(prs, &compiler);
    return (NeonAstExpression*)method;
}

static NeonAstExpression* neon_astparser_parseclass(NeonAstParser* prs)
{
    bool finishedparsingfields;
    bool fieldisstatic;
    size_t line;
    bool isstatic;
    NeonString* name;
    NeonString* super;
    NeonAstClassExpr* klass;
    NeonAstExpression* var;
    NeonAstExpression* method;
    if(setjmp(prs_jmpbuffer))
    {
        return NULL;
    }
    line = prs->previous.line;
    isstatic = prs->previous.type == NEON_TOK_KWSTATIC;
    if(isstatic)
    {
        neon_astparser_consume(prs, NEON_TOK_KWCLASS, "'class' after 'static'");
    }
    neon_astparser_consume(prs, NEON_TOK_IDENT, "class name after 'class'");
    name = neon_string_copy(prs->state, prs->previous.start, prs->previous.length);
    super = NULL;
    if(neon_astparser_match(prs, NEON_TOK_COLON))
    {
        neon_astparser_consume(prs, NEON_TOK_IDENT, "super class name after ':'");
        super = neon_string_copy(prs->state, prs->previous.start, prs->previous.length);
        if(super == name)
        {
            neon_astparser_raiseerror(prs, "class cannot inherit itself");
        }
    }
    klass = neon_ast_make_classexpr(prs->state, line, name, super);
    neon_astparser_ignorenewlines(prs, true);
    neon_astparser_consume(prs, NEON_TOK_BRACEOPEN, "'{' before class body");
    neon_astparser_ignorenewlines(prs, true);
    finishedparsingfields = false;
    while(!neon_astparser_check(prs, NEON_TOK_BRACECLOSE))
    {
        fieldisstatic = false;
        if(neon_astparser_match(prs, NEON_TOK_KWSTATIC))
        {
            fieldisstatic = true;
            if(neon_astparser_match(prs, NEON_TOK_KWVAR))
            {
                if(finishedparsingfields)
                {
                    neon_astparser_raiseerror(prs, "all static fields must be defined before the methods");
                }
                var = neon_astparser_parsevar_declaration(prs, true);
                if(var != NULL)
                {
                    neon_exprlist_push(prs->state, &klass->fields, var);
                }
                neon_astparser_ignorenewlines(prs, true);
                continue;
            }
            else
            {
                finishedparsingfields = true;
            }
        }
        method = neon_astparser_parsemethod(prs, isstatic || fieldisstatic);
        if(method != NULL)
        {
            neon_exprlist_push(prs->state, &klass->fields, method);
        }
        neon_astparser_ignorenewlines(prs, true);
    }
    neon_astparser_consume(prs, NEON_TOK_BRACECLOSE, "'}' after class body");
    return (NeonAstExpression*)klass;
}

static void neon_astparser_sync(NeonAstParser* prs)
{
    prs->panic_mode = false;
    while(prs->current.type != NEON_TOK_EOF)
    {
        if(prs->previous.type == NEON_TOK_NEWLINE)
        {
            longjmp(prs_jmpbuffer, 1);
            return;
        }
        switch(prs->current.type)
        {
            case NEON_TOK_KWCLASS:
            case NEON_TOK_KWFUNCTION:
            case NEON_TOK_KWEXPORT:
            case NEON_TOK_KWVAR:
            case NEON_TOK_KWCONST:
            case NEON_TOK_KWFOR:
            case NEON_TOK_KWSTATIC:
            case NEON_TOK_KWIF:
            case NEON_TOK_KWWHILE:
            case NEON_TOK_KWRETURN:
            {
                longjmp(prs_jmpbuffer, 1);
                return;
            }
            default:
            {
                neon_astparser_advance(prs);
            }
        }
    }
}

static NeonAstExpression* neon_astparser_parsedeclaration(NeonAstParser* prs)
{
    NeonAstExpression* statement;
    statement = NULL;
    if(neon_astparser_match(prs, NEON_TOK_KWCLASS) || neon_astparser_match(prs, NEON_TOK_KWSTATIC))
    {
        statement = neon_astparser_parseclass(prs);
    }
    else
    {
        statement = neon_astparser_parsestatement(prs);
    }
    return statement;
}

bool neon_astparser_parsesource(NeonAstParser* prs, const char* filename, const char* source, size_t srclength, NeonAstExprList* statements)
{
    NeonAstCompiler compiler;
    NeonAstExpression* statement;
    prs->haderror = false;
    prs->panic_mode = false;
    neon_astlex_init(prs->state, prs->state->scanner, filename, source, srclength);
    neon_astparser_initcompiler(prs, &compiler);
    neon_astparser_advance(prs);
    neon_astparser_ignorenewlines(prs, true);
    if(!prs_is_at_end(prs))
    {
        do
        {
            statement = neon_astparser_parsedeclaration(prs);
            if(statement != NULL)
            {
                neon_exprlist_push(prs->state, statements, statement);
            }
            if(!neon_astparser_matchnewline(prs))
            {
                if(neon_astparser_match(prs, NEON_TOK_EOF))
                {
                    break;
                }
            }
        } while(!prs_is_at_end(prs));
    }
    return prs->haderror || prs->state->scanner->haderror;
}

