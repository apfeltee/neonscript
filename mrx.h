
#pragma once

/************
 
    REMIMU: SINGLE HEADER C/C++ REGEX LIBRARY

    Compatible with C99 and C++11 and later standards. Uses backtracking and relatively standard regex syntax.

    #include "myregex.h"

FUNCTIONS

    //  Returns 0 on success, or -1 on invalid or unsupported regex, or -2 on not enough tokens given to parse regex.
    REMIMU_INLINE int mrx_regex_parse(
        RegexContext* ctx,
        //  Regex pattern to parse. Must be null-terminated.
        const char * pattern,       
        //  Output buffer of tokencount regex tokens
        RegexToken * tokens,
        //  Maximum allowed number of tokens to write
        int16_t * tokencount,
        // Optional bitflags.
        int32_t flags
        
    )
    
    // Returns match length, or -1 on no match, or -2 on out of memory, or -3 ifthe regex is invalid.
    REMIMU_INLINE int64_t mrx_regex_match(
        RegexContext* ctx,
        // Parsed regex to match against text.
        const RegexToken * tokens,
        // Text to match against tokens.
        const char * text,
        // index value to match at.
        size_t starti,
        // Number of allowed capture info output slots.
        uint16_t capslots,
        // Capture position info output buffer.
        int64_t* cappos,
        // Capture length info output buffer.
        int64_t* capspan
    ) 
    
    REMIMU_INLINE void mrx_regex_printtokens(
        // Regex tokens to spew to stdout, fordebugging.
        RegexToken* tokens
    )

PERFORMANCE

    On simple cases, Remimu's match speed is similar to PCRE2. Regex parsing/compilation is also much faster (around 4x to 10x), so single-shot regexes are often faster than PCRE2.

    HOWEVER: Remimu is a pure backtracking engine, and has `O(2^x)` complexity on regexes with catastrophic backtracking. It can be much, much, MUCH slower than PCRE2. Beware!

    Remimu uses length-checked fixed memory buffers with no recursion, so memory usage is statically known.

FEATURES

    - Lowest-common-denominator common regex syntax
    - Based on backtracking (slow in the worst case, but fast in the best case)
    - 8-bit only, no utf-16 or utf-32
    - Statically known memory usage (no heap allocation or recursion)
    - Groups with or without capture, and with or without quantifiers
    - Supported escapes:
    - - 2-digit hex: e.g. \x00, \xFF, or lowercase, or mixed case
    - - \r, \n, \t, \v, \f (whitespace characters)
    - - \d, \s, \w, \D, \S, \W (digit, space, and word character classes)
    - - \b, \B word boundary and non-word-boundary anchors (not fully supported in zero-size quantified groups, but even then, usually supported)
    - - Escaped literal characters: {}[]-()|^$*+?:./\
    - - - Escapes work in character classes, except for'b'
    - Character classes, including disjoint ranges, proper handling of bare [ and trailing -, etc
    - - Dot (.) matches all characters, including newlines, unless REMIMU_FLAG_DOT_NO_NEWLINES is passed as a flag to mrx_regex_parse
    - - Dot (.) only matches at most one byte at a time, so matching \r\n requires two dots (and not using REMIMU_FLAG_DOT_NO_NEWLINES)
    - Anchors (^ and $)
    - - Same support caveats as \b, \B apply
    - Basic quantifiers (*, +, ?)
    - - Quantifiers are greedy by default.
    - Explicit quantifiers ({2}, {5}, {5,}, {5,7})
    - Alternation e.g. (asdf|foo)
    - Lazy quantifiers e.g. (asdf)*? or \w+?
    - Possessive greedy quantifiers e.g. (asdf)*+ or \w++
    - - NOTE: Capture groups forand inside of possessive groups return no capture information.
    - Atomic groups e.g. (?>(asdf))
    - - NOTE: Capture groups inside of atomic groups return no capture information.

NOT SUPPORTED

    - Strings with non-terminal null characters
    - Unicode character classes (matching single utf-8 characters works regardless)
    - Exact POSIX regex semantics (posix-style greediness etc)
    - Backreferences
    - Lookbehind/Lookahead
    - Named groups
    - Most other weird flavor-specific regex stuff
    - Capture of or inside of possessive-quantified groups (still take up a capture slot, but no data is returned)

USAGE

    // minimal:
    
    RegexContext ctx; 
    RegexToken tokens[1024];
    int16_t tokencount = 1024;
    mrx_context_init(&ctx);
    int e = mrx_regex_parse(&ctx, "[0-9]+\\.[0-9]+", tokens, &tokencount, 0);
    assert(!e);
    
    int64_t matchlen = mrx_regex_match(&ctx, tokens, "23.53) ", 0, 0, 0, 0);
    printf("########### return: %d\n", matchlen);
    
    // with captures:
    RegexContext ctx; 
    RegexToken tokens[256];
    int16_t tokencount = sizeof(tokens)/sizeof(tokens[0]);
    mrx_context_init(&xtx);
    int e = mrx_regex_parse(&ctx, "((a)|(b))++", tokens, &tokencount, 0);
    assert(!e);
    
    int64_t cappos[5];
    int64_t capspan[5];
    memset(cappos, 0xFF, sizeof(cappos));
    memset(capspan, 0xFF, sizeof(capspan));
    
    int64_t matchlen = mrx_regex_match(&ctx, tokens, "aaaaaabbbabaqa", 0, 5, cappos, capspan);
    printf("Match length: %d\n", matchlen);
    for(int i = 0; i < 5; i++)
        printf("Capture %d: %d plus %d\n", i, cappos[i], capspan[i]);
    mrx_regex_printtokens(tokens);

LICENSE

    Creative Commons Zero, public domain.

*/

#include <stdint.h>
#include <stdio.h>
#include <stddef.h>
#include <string.h>
#include <assert.h>

#if defined(__STRICT_ANSI__)
    #define REMIMU_INLINE static
#else
    #define REMIMU_INLINE static inline
#endif

#define MRX_VERBOSE 0

enum
{
    REMIMU_FLAG_DOT_NO_NEWLINES = 1
};

enum
{
    REMIMU_KIND_NORMAL = 0,
    REMIMU_KIND_OPEN = 1,
    REMIMU_KIND_NCOPEN = 2,
    REMIMU_KIND_CLOSE = 3,
    REMIMU_KIND_OR = 4,
    REMIMU_KIND_CARET = 5,
    REMIMU_KIND_DOLLAR = 6,
    REMIMU_KIND_BOUND = 7,
    REMIMU_KIND_NBOUND = 8,
    REMIMU_KIND_END = 9
};

enum
{
    REMIMU_MODE_POSSESSIVE = 1,
    REMIMU_MODE_LAZY = 2,
    /*  temporary; gets cleared later */
    REMIMU_MODE_INVERTED = 128
};

enum
{
    /*
    0: init
    1: normal
    2: in char class, initial state
    3: in char class, but possibly looking fora range marker
    4: in char class, but just saw a range marker
    5: immediately after quantifiable token
    6: immediately after quantifier
    */
    STATE_NORMAL = 1,
    STATE_QUANT = 2,
    STATE_MODE = 3,
    STATE_CC_INIT = 4,
    STATE_CC_NORMAL = 5,
    STATE_CC_RANGE = 6
};

typedef struct RegexMatcherState RegexMatcherState;
typedef struct RegexContext RegexContext;
typedef struct RegexToken RegexToken;

struct RegexMatcherState
{
    uint32_t k;
    uint32_t group_state; /*  quantified group temp state (e.g. number of repetitions) */
    uint32_t prev; /*  for)s, stack index of corresponding previous quantified state */
    uint64_t i;
    uint64_t range_min;
    uint64_t range_max;
};


struct RegexToken
{
    uint8_t kind;
    uint8_t mode;
    uint16_t count_lo;
    /*  0 means no limit */
    uint16_t count_hi;
    /*  forgroups: mask 0 stores group-with-quantifier number (quantifiers are +, *, ?, {n}, {n,}, or {n,m}) */
    uint16_t mask[16];
    /*  from ( or ), offset in token list to matching paren. TODO: move into mask maybe */
    int16_t pair_offset;
};


struct RegexContext
{
    bool haderror;
    bool isallocated;
    size_t maxtokens;
    size_t tokencount;
    char errorbuf[1024];
    RegexToken* tokens;
};

REMIMU_INLINE int mrx_regex_parse(RegexContext* ctx, const char* pattern, int32_t flags);
REMIMU_INLINE int64_t mrx_regex_match(RegexContext* ctx, const char* text, size_t starti, uint16_t capslots, int64_t* cappos, int64_t* capspan);
REMIMU_INLINE void mrx_regex_printtokens(RegexToken* tokens);

#define MRX_DO_INVERT() \
    { \
        for(macn = 0; macn < 16; macn++) \
        { \
            token.mask[macn] = ~token.mask[macn]; \
        } \
        token.mode &= ~REMIMU_MODE_INVERTED; \
    }

#define MRX_CLEAR_TOKEN(TOKEN) \
    { \
        memset(&(TOKEN), 0, sizeof(RegexToken)); \
        token.count_lo = 1; \
        token.count_hi = 2; \
    }

#define MRX_PUSH_TOKEN() \
    { \
        if(k == 0 || ctx->tokens[k - 1].kind != token.kind || (token.kind != REMIMU_KIND_BOUND && token.kind != REMIMU_KIND_NBOUND)) \
        { \
            if(token.mode & REMIMU_MODE_INVERTED) \
            { \
                MRX_DO_INVERT(); \
            } \
            if(k >= tokenslen) \
            { \
                puts("buffer overflow"); \
                return -2; \
            } \
            ctx->tokens[k++] = token; \
            MRX_CLEAR_TOKEN(token); \
        } \
    }

#define MRX_SET_MASK(byte) \
    { \
        token.mask[((uint8_t)(byte)) >> 4] |= 1 << ((uint8_t)(byte) & 0xF); \
    }

#define MRX_SET_MASK_ALL() \
    { \
        for(macn = 0; macn < 16; macn++) \
        { \
            token.mask[macn] = 0xFFFF; \
        } \
    }

REMIMU_INLINE void mrx_context_initctx(RegexContext* ctx, RegexToken* tokens, size_t maxtokens, bool onstack)
{
    ctx->isallocated = (onstack ? false : true);
    ctx->haderror = false;
    ctx->tokens = tokens;
    ctx->maxtokens = maxtokens;
}


REMIMU_INLINE RegexContext* mrx_context_init(RegexToken* tokens, size_t maxtokens)
{
    RegexContext* ctx;
    ctx = (RegexContext*)malloc(sizeof(RegexContext));
    if(ctx == NULL)
    {
        return NULL;
    }
    mrx_context_initctx(ctx, tokens, maxtokens, false);
    return ctx;
}

void mrx_context_destroy(RegexContext* ctx)
{
    if(!ctx->isallocated)
    {
        return;
    }
    free(ctx);
}

void mrx_context_seterror(RegexContext* ctx, const char* fmt, ...)
{
    va_list va;
    ctx->haderror = true;
    fprintf(stderr, "ERROR: ");
    va_start(va, fmt);
    vsprintf(ctx->errorbuf, fmt, va);
    va_end(va);
}

REMIMU_INLINE int mrx_util_isquantchar(int c)
{
    return (c == '{' || c == '}' || c == '[' || c == ']' || c == '-' || c == '(' || c == ')' || c == '|' || c == '^' || c == '$' || c == '*' || c == '+'
            || c == '?' || c == ':' || c == '.' || c == '/' || c == '\\');
}

/*
 Returns a negative number on failure:
 -1: Regex string is invalid or using unsupported features or too long.
 -2: Provided buffer not long enough. Give up, or reallocate with more length and retry.
  Returns 0 on success.
  On call, tokencount pointer must point to the number of tokens that can be written to the tokens buffer.
  On successful return, the number of actually used tokens is written to tokencount.
  Sets tokencount to zero ifa regex is not created but no error happened (e.g. empty pattern).
  Flags: Not yet used.
  SAFETY: Pattern must be null-terminated.
  SAFETY: tokens buffer must have at least the input tokencount number of RegexToken objects. They are allowed to be uninitialized.
*/
REMIMU_INLINE int mrx_regex_parse(RegexContext* ctx, const char* pattern, int32_t flags)
{
    int escstate;
    int state;
    int charclassmem;
    int parencount;
    int16_t k;
    int64_t tokenslen;
    uint64_t i;
    uint64_t mi;
    uint64_t patternlen;
    uint8_t clsi;
    char c;
    ptrdiff_t l;
    int macn;
    uint32_t val;
    uint32_t val2;
    uint8_t escc;
    uint8_t n0;
    uint8_t n1;
    uint8_t isupper;
    uint64_t n;
    int16_t k3;
    int16_t k2;
    ptrdiff_t diff;
    int balance;
    ptrdiff_t found;
    uint16_t m[16];
    RegexToken token;
    tokenslen = ctx->maxtokens;
    patternlen = strlen(pattern);
    if(ctx->maxtokens == 0)
    {
        return -2;
    }
    /*
    0: normal
    1: just saw a backslash
    */
    escstate = 0;
    state = STATE_NORMAL;
    charclassmem = -1;
    MRX_CLEAR_TOKEN(token);
    k = 0;
    /*
    start with an invisible group specifier
    (this allows the matcher to not need to have a special root-level alternation operator case)
    */
    token.kind = REMIMU_KIND_OPEN;
    token.count_lo = 0;
    token.count_hi = 0;
    parencount = 0;
    for(i = 0; i < patternlen; i++)
    {
        c = pattern[i];
        if(state == STATE_QUANT)
        {
            state = STATE_MODE;
            if(c == '?')
            {
                /* first non-allowed amount */
                token.count_lo = 0;
                token.count_hi = 2;
                continue;
            }
            else if(c == '+')
            {
                /* unlimited */
                token.count_lo = 1;
                token.count_hi = 0;
                continue;
            }
            else if(c == '*')
            {
                /* unlimited */
                token.count_lo = 0;
                token.count_hi = 0;
                continue;
            }
            else if(c == '{')
            {
                if(pattern[i + 1] == 0 || pattern[i + 1] < '0' || pattern[i + 1] > '9')
                {
                    state = STATE_NORMAL;
                }
                else
                {
                    i += 1;
                    val = 0;
                    while(pattern[i] >= '0' && pattern[i] <= '9')
                    {
                        val *= 10;
                        val += (uint32_t)(pattern[i] - '0');
                        if(val > 0xFFFF)
                        {
                            /*  unsupported length */
                            mrx_context_seterror(ctx, "quantifier range too long");
                            return -1;
                        }
                        i += 1;
                    }
                    token.count_lo = val;
                    token.count_hi = val + 1;
                    if(pattern[i] == ',')
                    {
                        token.count_hi = 0; /*  unlimited */
                        i += 1;

                        if(pattern[i] >= '0' && pattern[i] <= '9')
                        {
                            val2 = 0;
                            while(pattern[i] >= '0' && pattern[i] <= '9')
                            {
                                val2 *= 10;
                                val2 += (uint32_t)(pattern[i] - '0');
                                if(val2 > 0xFFFF)
                                {
                                    /*  unsupported length */
                                    mrx_context_seterror(ctx, "quantifier range too long");
                                    return -1;
                                }
                                i += 1;
                            }
                            if(val2 < val)
                            {
                                mrx_context_seterror(ctx,"quantifier range is backwards");
                                return -1; /*  unsupported length */
                            }
                            token.count_hi = val2 + 1;
                        }
                    }
                    if(pattern[i] == '}')
                    {
                        /*  quantifier range parsed successfully */
                        continue;
                    }
                    else
                    {
                        mrx_context_seterror(ctx, "quantifier range syntax broken (no terminator)");
                        return -1;
                    }
                }
            }
        }
        if(state == STATE_MODE)
        {
            state = STATE_NORMAL;
            if(c == '?')
            {
                token.mode |= REMIMU_MODE_LAZY;
                continue;
            }
            else if(c == '+')
            {
                token.mode |= REMIMU_MODE_POSSESSIVE;
                continue;
            }
        }
        if(state == STATE_NORMAL)
        {
            if(escstate == 1)
            {
                escstate = 0;
                if(c == 'n')
                {
                    MRX_SET_MASK('\n')
                }
                else if(c == 'r')
                {
                    MRX_SET_MASK('\r')
                }
                else if(c == 't')
                {
                    MRX_SET_MASK('\t')
                }
                else if(c == 'v')
                {
                    MRX_SET_MASK('\v')
                }
                else if(c == 'f')
                {
                    MRX_SET_MASK('\f')
                }
                else if(c == 'x')
                {
                    if(pattern[i + 1] == 0 || pattern[i + 2] == 0)
                    {
                        return -1; /*  too-short hex pattern */
                    }
                    n0 = pattern[i + 1];
                    n1 = pattern[i + 1];
                    if(n0 < '0' || n0 > 'f' || n1 < '0' || n1 > 'f' || (n0 > '9' && n0 < 'A') || (n1 > '9' && n1 < 'A'))
                    {
                        mrx_context_seterror(ctx, "invalid hex digit");
                        return -1; /*  invalid hex digit */
                    }
                    if(n0 > 'F')
                    {
                        n0 -= 0x20;
                    }
                    if(n1 > 'F')
                    {
                        n1 -= 0x20;
                    }
                    if(n0 >= 'A')
                    {
                        n0 -= 'A' - 10;
                    }
                    if(n1 >= 'A')
                    {
                        n1 -= 'A' - 10;
                    }
                    n0 -= '0';
                    n1 -= '0';
                    MRX_SET_MASK((n1 << 4) | n0)
                    i += 2;
                }
                else if(mrx_util_isquantchar(c))
                {
                    MRX_SET_MASK(c)
                    state = STATE_QUANT;
                }
                else if(c == 'd' || c == 's' || c == 'w' || c == 'D' || c == 'S' || c == 'W')
                {
                    isupper = c <= 'Z';
                    memset(m, 0, sizeof(m));
                    if(isupper)
                    {
                        c += 0x20;
                    }
                    if(c == 'd' || c == 'w')
                    {
                        m[3] |= 0x03FF; /*  0~7 */
                    }
                    if(c == 's')
                    {
                        m[0] |= 0x3E00; /*  \t-\r (includes \n, \v, and \f in the middle. 5 enabled bits.) */
                        m[2] |= 1; /*  ' ' */
                    }
                    if(c == 'w')
                    {
                        m[4] |= 0xFFFE; /*  A-O */
                        m[5] |= 0x87FF; /*  P-Z_ */
                        m[6] |= 0xFFFE; /*  a-o */
                        m[7] |= 0x07FF; /*  p-z */
                    }
                    for(mi = 0; mi < 16; mi++)
                    {
                        token.mask[mi] |= isupper ? ~m[mi] : m[mi];
                    }
                    token.kind = REMIMU_KIND_NORMAL;
                    state = STATE_QUANT;
                }
                else if(c == 'b')
                {
                    token.kind = REMIMU_KIND_BOUND;
                    state = STATE_NORMAL;
                }
                else if(c == 'B')
                {
                    token.kind = REMIMU_KIND_NBOUND;
                    state = STATE_NORMAL;
                }
                else
                {
                    mrx_context_seterror(ctx, "unsupported escape sequence");
                    return -1; /*  unknown/unsupported escape sequence */
                }
            }
            else
            {
                MRX_PUSH_TOKEN();
                if(c == '\\')
                {
                    escstate = 1;
                }
                else if(c == '[')
                {
                    state = STATE_CC_INIT;
                    charclassmem = -1;
                    token.kind = REMIMU_KIND_NORMAL;
                    if(pattern[i + 1] == '^')
                    {
                        token.mode |= REMIMU_MODE_INVERTED;
                        i += 1;
                    }
                }
                else if(c == '(')
                {
                    parencount += 1;
                    state = STATE_NORMAL;
                    token.kind = REMIMU_KIND_OPEN;
                    token.count_lo = 0;
                    token.count_hi = 1;
                    if(pattern[i + 1] == '?' && pattern[i + 2] == ':')
                    {
                        token.kind = REMIMU_KIND_NCOPEN;
                        i += 2;
                    }
                    else if(pattern[i + 1] == '?' && pattern[i + 2] == '>')
                    {
                        token.kind = REMIMU_KIND_NCOPEN;
                        MRX_PUSH_TOKEN();
                        state = STATE_NORMAL;
                        token.kind = REMIMU_KIND_NCOPEN;
                        token.mode = REMIMU_MODE_POSSESSIVE;
                        token.count_lo = 1;
                        token.count_hi = 2;
                        i += 2;
                    }
                }
                else if(c == ')')
                {
                    parencount -= 1;
                    if(parencount < 0 || k == 0)
                    {
                        mrx_context_seterror(ctx, "unbalanced parentheses");
                        return -1; /*  unbalanced parens */
                    }
                    token.kind = REMIMU_KIND_CLOSE;
                    state = STATE_QUANT;
                    balance = 0;
                    found = -1;
                    for(l = k - 1; l >= 0; l--)
                    {
                        if(ctx->tokens[l].kind == REMIMU_KIND_NCOPEN || ctx->tokens[l].kind == REMIMU_KIND_OPEN)
                        {
                            if(balance == 0)
                            {
                                found = l;
                                break;
                            }
                            else
                            {
                                balance -= 1;
                            }
                        }
                        else if(ctx->tokens[l].kind == REMIMU_KIND_CLOSE)
                        {
                            balance += 1;
                        }
                    }
                    if(found == -1)
                    {
                        mrx_context_seterror(ctx, "unbalanced parentheses");
                        return -1; /*  unbalanced parens */
                    }
                    diff = k - found;
                    if(diff > 32767)
                    {
                        mrx_context_seterror(ctx, "difference too large");
                        return -1; /*  too long */
                    }
                    token.pair_offset = -diff;
                    ctx->tokens[found].pair_offset = diff;
                    /*  phantom group foratomic group emulation */
                    if(ctx->tokens[found].mode == REMIMU_MODE_POSSESSIVE)
                    {
                        MRX_PUSH_TOKEN();
                        token.kind = REMIMU_KIND_CLOSE;
                        token.mode = REMIMU_MODE_POSSESSIVE;
                        token.pair_offset = -diff - 2;
                        ctx->tokens[found - 1].pair_offset = diff + 2;
                    }
                }
                else if(c == '?' || c == '+' || c == '*' || c == '{')
                {
                    mrx_context_seterror(ctx, "quantifier in non-quantifier context");
                    return -1; /*  quantifier in non-quantifier context */
                }
                else if(c == '.')
                {
                    /* puts("setting ALL of mask..."); */
                    MRX_SET_MASK_ALL();
                    if(flags & REMIMU_FLAG_DOT_NO_NEWLINES)
                    {
                        token.mask[1] ^= 0x04; /*  \n */
                        token.mask[1] ^= 0x20; /*  \r */
                    }
                    state = STATE_QUANT;
                }
                else if(c == '^')
                {
                    token.kind = REMIMU_KIND_CARET;
                    state = STATE_NORMAL;
                }
                else if(c == '$')
                {
                    token.kind = REMIMU_KIND_DOLLAR;
                    state = STATE_NORMAL;
                }
                else if(c == '|')
                {
                    token.kind = REMIMU_KIND_OR;
                    state = STATE_NORMAL;
                }
                else
                {
                    MRX_SET_MASK(c);
                    state = STATE_QUANT;
                }
            }
        }
        else if(state == STATE_CC_INIT || state == STATE_CC_NORMAL || state == STATE_CC_RANGE)
        {
            if(c == '\\' && escstate == 0)
            {
                escstate = 1;
                continue;
            }
            escc = 0;
            if(escstate == 1)
            {
                escstate = 0;
                if(c == 'n')
                {
                    escc = '\n';
                }
                else if(c == 'r')
                {
                    escc = '\r';
                }
                else if(c == 't')
                {
                    escc = '\t';
                }
                else if(c == 'v')
                {
                    escc = '\v';
                }
                else if(c == 'f')
                {
                    escc = '\f';
                }
                else if(c == 'x')
                {
                    if(pattern[i + 1] == 0 || pattern[i + 2] == 0)
                    {
                        mrx_context_seterror(ctx, "hex pattern too short");
                        return -1; /*  too-short hex pattern */
                    }
                    n0 = pattern[i + 1];
                    n1 = pattern[i + 1];
                    if(n0 < '0' || n0 > 'f' || n1 < '0' || n1 > 'f' || (n0 > '9' && n0 < 'A') || (n1 > '9' && n1 < 'A'))
                    {
                        mrx_context_seterror(ctx, "invalid hex digit");
                        return -1; /*  invalid hex digit */
                    }
                    if(n0 > 'F')
                    {
                        n0 -= 0x20;
                    }
                    if(n1 > 'F')
                    {
                        n1 -= 0x20;
                    }
                    if(n0 >= 'A')
                    {
                        n0 -= 'A' - 10;
                    }
                    if(n1 >= 'A')
                    {
                        n1 -= 'A' - 10;
                    }
                    n0 -= '0';
                    n1 -= '0';
                    escc = (n1 << 4) | n0;
                    i += 2;
                }
                else if(c == '{' || c == '}' || c == '[' || c == ']' || c == '-' || c == '(' || c == ')' || c == '|' || c == '^' || c == '$' || c == '*'
                        || c == '+' || c == '?' || c == ':' || c == '.' || c == '/' || c == '\\')
                {
                    escc = c;
                }
                else if(c == 'd' || c == 's' || c == 'w' || c == 'D' || c == 'S' || c == 'W')
                {
                    if(state == STATE_CC_RANGE)
                    {
                        mrx_context_seterror(ctx, "tried to use a shorthand as part of a range");
                        return -1; /*  range shorthands can't be part of a range */
                    }
                    isupper = c <= 'Z';
                    memset(m, 0, sizeof(m));
                    if(isupper)
                    {
                        c += 0x20;
                    }
                    if(c == 'd' || c == 'w')
                    {
                        m[3] |= 0x03FF; /*  0~7 */
                    }
                    if(c == 's')
                    {
                        m[0] |= 0x3E00; /*  \t-\r (includes \n, \v, and \f in the middle. 5 enabled bits.) */
                        m[2] |= 1; /*  ' ' */
                    }
                    if(c == 'w')
                    {
                        m[4] |= 0xFFFE; /*  A-O */
                        m[5] |= 0x87FF; /*  P-Z_ */
                        m[6] |= 0xFFFE; /*  a-o */
                        m[7] |= 0x07FF; /*  p-z */
                    }
                    for(mi = 0; mi < 16; mi++)
                    {
                        token.mask[mi] |= isupper ? ~m[mi] : m[mi];
                    }
                    charclassmem = -1; /*  range shorthands can't be part of a range */
                    continue;
                }
                else
                {
                    printf("unknown/unsupported escape sequence in character class (\\%c)\n", c);
                    return -1; /*  unknown/unsupported escape sequence */
                }
            }
            if(state == STATE_CC_INIT)
            {
                charclassmem = c;
                MRX_SET_MASK(c);
                state = STATE_CC_NORMAL;
            }
            else if(state == STATE_CC_NORMAL)
            {
                if(c == ']' && escc == 0)
                {
                    charclassmem = -1;
                    state = STATE_QUANT;
                    continue;
                }
                else if(c == '-' && escc == 0 && charclassmem >= 0)
                {
                    state = STATE_CC_RANGE;
                    continue;
                }
                else
                {
                    charclassmem = c;
                    MRX_SET_MASK(c);
                    state = STATE_CC_NORMAL;
                }
            }
            else if(state == STATE_CC_RANGE)
            {
                if(c == ']' && escc == 0)
                {
                    charclassmem = -1;
                    MRX_SET_MASK('-');
                    state = STATE_QUANT;
                    continue;
                }
                else
                {
                    if(charclassmem == -1)
                    {
                        mrx_context_seterror(ctx, "character class range is broken");
                        return -1; /*  probably tried to use a character class shorthand as part of a range */
                    }
                    if((uint8_t)c < charclassmem)
                    {
                        mrx_context_seterror(ctx, "character class range is misordered");
                        return -1; /*  range is in wrong order */
                    }
                    /* printf("enabling char class from %d to %d...\n", charclassmem, c); */
                    for(clsi = c; clsi > charclassmem; clsi--)
                    {
                        MRX_SET_MASK(clsi);
                    }
                    state = STATE_CC_NORMAL;
                    charclassmem = -1;
                }
            }
        }
        else
        {
            assert(0);
        }
    }
    if(parencount > 0)
    {
        mrx_context_seterror(ctx, "(parencount > 0)");
        return -1; /*  unbalanced parens */
    }
    if(escstate != 0)
    {
        mrx_context_seterror(ctx, "(escstate != 0)");
        return -1; /*  open escape sequence */
    }
    if(state >= STATE_CC_INIT)
    {
        mrx_context_seterror(ctx, "(state >= STATE_CC_INIT)");
        return -1; /*  open character class */
    }
    MRX_PUSH_TOKEN();
    /*  add invisible non-capturing group specifier */
    token.kind = REMIMU_KIND_CLOSE;
    token.count_lo = 1;
    token.count_hi = 2;
    MRX_PUSH_TOKEN();
    /*  add end token (tells matcher that it's done) */
    token.kind = REMIMU_KIND_END;
    MRX_PUSH_TOKEN();
    ctx->tokens[0].pair_offset = k - 2;
    ctx->tokens[k - 2].pair_offset = -(k - 2);
    ctx->tokencount = k;
    /*  copy quantifiers from )s to (s (so (s know whether they're optional) */
    /*  also take the opportunity to smuggle "quantified group index" into the mask field forthe ) */
    n = 0;
    for(k2 = 0; k2 < k; k2++)
    {
        if(ctx->tokens[k2].kind == REMIMU_KIND_CLOSE)
        {
            ctx->tokens[k2].mask[0] = n++;
            k3 = k2 + ctx->tokens[k2].pair_offset;
            ctx->tokens[k3].count_lo = ctx->tokens[k2].count_lo;
            ctx->tokens[k3].count_hi = ctx->tokens[k2].count_hi;
            ctx->tokens[k3].mask[0] = n++;
            ctx->tokens[k3].mode = ctx->tokens[k2].mode;
            /* if(n > 65535) */
            if(n > 1024)
            {
                return -1; /*  too many quantified groups */
            }
        }
        else if(ctx->tokens[k2].kind == REMIMU_KIND_OR || ctx->tokens[k2].kind == REMIMU_KIND_OPEN || ctx->tokens[k2].kind == REMIMU_KIND_NCOPEN)
        {
            /*  find next | or ) and how far away it is. store in token */
            balance = 0;
            found = -1;
            for(l = k2 + 1; l < tokenslen; l++)
            {
                if(ctx->tokens[l].kind == REMIMU_KIND_OR && balance == 0)
                {
                    found = l;
                    break;
                }
                else if(ctx->tokens[l].kind == REMIMU_KIND_CLOSE)
                {
                    if(balance == 0)
                    {
                        found = l;
                        break;
                    }
                    else
                    {
                        balance -= 1;
                    }
                }
                else if(ctx->tokens[l].kind == REMIMU_KIND_NCOPEN || ctx->tokens[l].kind == REMIMU_KIND_OPEN)
                {
                    balance += 1;
                }
            }
            if(found == -1)
            {
                mrx_context_seterror(ctx, "unbalanced parens...");
                return -1; /*  unbalanced parens */
            }
            diff = found - k2;
            if(diff > 32767)
            {
                mrx_context_seterror(ctx, "too long...");
                return -1; /*  too long */
            }
            if(ctx->tokens[k2].kind == REMIMU_KIND_OR)
            {
                ctx->tokens[k2].pair_offset = diff;
            }
            else
            {
                ctx->tokens[k2].mask[15] = diff;
            }
        }
    }
    return 0;
}

#undef MRX_PUSH_TOKEN
#undef MRX_SET_MASK
#undef MRX_CLEAR_TOKEN

/*  NOTE: undef'd later */
#define _REGEX_CHECK_MASK(tokens, K, byte) \
    (!!(tokens[K].mask[((uint8_t)byte) >> 4] & (1 << ((uint8_t)byte & 0xF))))

#if defined(MRX_VERBOSE) && (MRX_VERBOSE == 1)
    #define MRX_IFVERBOSE(X) \
        { \
            { \
                X \
            } \
        }
#else
    #define MRX_IFVERBOSE(X)
#endif

#define _P_TEXT_HIGHLIGHTED() \
    { \
        MRX_IFVERBOSE({ \
            uint64_t hlq; \
            printf("\033[91m"); \
            for(hlq = 0; hlq < i; hlq++) \
            { \
                printf("%c", text[hlq]); \
            } \
            printf("\033[0m"); \
            for(hlq = i; text[hlq] != 0; hlq++) \
            { \
                printf("%c", text[hlq]); \
            } \
            printf("\n"); \
        }); \
    }

#define MRX_REWIND_DO_SAVE_RAW(K, ISDUMMY) \
    { \
        RegexMatcherState s; \
        if(stackn >= stacksizemax) \
        { \
            mrx_context_seterror(ctx, "out of backtracking room. returning"); \
            return -2; \
        } \
        memset(&s, 0, sizeof(RegexMatcherState)); \
        s.i = i; \
        s.k = (K); \
        s.range_min = range_min; \
        s.range_max = range_max; \
        s.prev = 0; \
        if(ISDUMMY) \
        { \
            s.prev = 0xFAC7; \
        } \
        else if(ctx->tokens[s.k].kind == REMIMU_KIND_CLOSE) \
        { \
            s.group_state = qgroupstate[ctx->tokens[s.k].mask[0]]; \
            s.prev = qgroupstack[ctx->tokens[s.k].mask[0]]; \
            qgroupstack[ctx->tokens[s.k].mask[0]] = stackn; \
        } \
        rewindstack[stackn++] = s; \
        _P_TEXT_HIGHLIGHTED(); \
        MRX_IFVERBOSE( \
        { printf("-- saving rewind state k %u i %ld rmin %ld rmax %ld (line %d) (depth %d prev %d)\n", s.k, i, range_min, range_max, __LINE__, stackn, s.prev); }) \
    }

#define MRX_REWIND_DO_SAVE_DUMMY(K) \
    MRX_REWIND_DO_SAVE_RAW(K, 1)

#define MRX_REWIND_DO_SAVE(K) \
    MRX_REWIND_DO_SAVE_RAW(K, 0)

#define MRX_REWIND_OR_ABORT() \
    { \
        if(stackn == 0) \
        { \
            return -1; \
        } \
        stackn -= 1; \
        while(stackn > 0 && rewindstack[stackn].prev == 0xFAC7) \
        { \
            stackn -= 1; \
        } \
        justrewinded = 1; \
        range_min = rewindstack[stackn].range_min; \
        range_max = rewindstack[stackn].range_max; \
        assert(rewindstack[stackn].i <= i); \
        i = rewindstack[stackn].i; \
        k = rewindstack[stackn].k; \
        if(ctx->tokens[k].kind == REMIMU_KIND_CLOSE) \
        { \
            qgroupstate[ctx->tokens[k].mask[0]] = rewindstack[stackn].group_state; \
            qgroupstack[ctx->tokens[k].mask[0]] = rewindstack[stackn].prev; \
        } \
        _P_TEXT_HIGHLIGHTED() \
        MRX_IFVERBOSE( \
        { printf("-- rewound to k %d i %ld rmin %ld rmax %ld (kind %d prev %d)\n", k, i, range_min, range_max, ctx->tokens[k].kind, rewindstack[stackn].prev); }) \
        k -= 1; \
    }
/*  the -= 1 is because of the k++ in the forloop */

#define _REGEX_CHECK_IS_W(byte) (!!(wmask[((uint8_t)byte) >> 4] & (1 << ((uint8_t)byte & 0xF))))

/*  Returns match length iftext starts with a regex match.
* Returns -1 ifthe text doesn't start with a regex match.
* Returns -2 ifthe matcher ran out of memory or the regex is too complex.
* Returns -3 ifthe regex is somehow invalid.
* The first capslots capture positions and spans (lengths) will be written to cappos and capspan. If zero, will not be written to.
* SAFETY: The text variable must be null-terminated, and starti must be the index of a character within the string or its null terminator.
* SAFETY: Tokens array must be terminated by a REMIMU_KIND_END token (done by default by mrx_regex_parse).
* SAFETY: Partial capture data may be written even ifthe match fails.
*/

REMIMU_INLINE int64_t mrx_regex_match(RegexContext* ctx, const char* text, size_t starti, uint16_t capslots, int64_t* cappos, int64_t* capspan)
{
    enum
    {
        stacksizemax = 1024,
        auxstatssize = 1024
    };

    int kind;
    size_t n;
    uint64_t tokenslen;
    uint32_t k;
    uint16_t caps;
    uint16_t stackn;
    uint64_t i;
    uint64_t range_min;
    uint64_t range_max;
    uint8_t justrewinded;
    size_t iterlimit;
    uint64_t origk;
    ptrdiff_t kdiff;
    uint32_t prev;
    uint8_t forcezero;
    uint32_t k2;
    uint64_t ntcnt;
    uint64_t oldi;
    uint64_t hiclimit;
    uint64_t rangelimit;
    uint16_t capindex;
    uint64_t wmask[16];
    /* quantified group state */
    uint8_t qgroupacceptszero[auxstatssize];
    /* number of repetitions */
    uint32_t qgroupstate[auxstatssize];
    /* location of most recent corresponding ) on stack. 0 means nowhere */
    uint32_t qgroupstack[auxstatssize];
    uint16_t qgroupcapindex[auxstatssize];
    RegexMatcherState rewindstack[stacksizemax];

    if(capslots > auxstatssize)
    {
        capslots = auxstatssize;
    }
    memset(qgroupcapindex, 0xFF, sizeof(qgroupcapindex));
    tokenslen = 0;
    k = 0;
    caps = 0;
    while(ctx->tokens[k].kind != REMIMU_KIND_END)
    {
        if(ctx->tokens[k].kind == REMIMU_KIND_OPEN && caps < capslots)
        {
            qgroupcapindex[ctx->tokens[k].mask[0]] = caps;
            qgroupcapindex[ctx->tokens[k + ctx->tokens[k].pair_offset].mask[0]] = caps;
            cappos[caps] = -1;
            capspan[caps] = -1;
            caps += 1;
        }
        k += 1;
        if(ctx->tokens[k].kind == REMIMU_KIND_CLOSE || ctx->tokens[k].kind == REMIMU_KIND_OPEN || ctx->tokens[k].kind == REMIMU_KIND_NCOPEN)
        {
            if(ctx->tokens[k].mask[0] >= auxstatssize)
            {
                mrx_context_seterror(ctx, "too many qualified groups. returning");
                /* OOM: too many quantified groups */
                return -2;
            }
            qgroupstate[ctx->tokens[k].mask[0]] = 0;
            qgroupstack[ctx->tokens[k].mask[0]] = 0;
            qgroupacceptszero[ctx->tokens[k].mask[0]] = 0;
        }
    }
    tokenslen = k;
    stackn = 0;
    i = starti;
    range_min = 0;
    range_max = 0;
    justrewinded = 0;
    /* used in boundary anchor checker */
    memset(wmask, 0, sizeof(wmask));
    wmask[3] = 0x03FF;
    wmask[4] = 0xFFFE;
    wmask[5] = 0x87FF;
    wmask[6] = 0xFFFE;
    wmask[7] = 0x07FF;
    iterlimit = 10000;
    for(k = 0; k < tokenslen; k++)
    {
        /* iterlimit--; */
        if(iterlimit == 0)
        {
            mrx_context_seterror(ctx, "iteration limit exceeded. returning");
            return -2;
        }
        MRX_IFVERBOSE({ printf("k: %d\ti: %ld\tl: %ld\tstackn: %d\n", k, i, iterlimit, stackn); });
        _P_TEXT_HIGHLIGHTED();
        if(ctx->tokens[k].kind == REMIMU_KIND_CARET)
        {
            if(i != 0)
            {
                MRX_REWIND_OR_ABORT();
            }
            continue;
        }
        else if(ctx->tokens[k].kind == REMIMU_KIND_DOLLAR)
        {
            if(text[i] != 0)
            {
                MRX_REWIND_OR_ABORT();
            }
            continue;
        }
        else if(ctx->tokens[k].kind == REMIMU_KIND_BOUND)
        {
            if(i == 0 && !_REGEX_CHECK_IS_W(text[i]))
            {
                MRX_REWIND_OR_ABORT();
            }
            else if(i != 0 && text[i] == 0 && !_REGEX_CHECK_IS_W(text[i - 1]))
            {
                MRX_REWIND_OR_ABORT();
            }
            else if(i != 0 && text[i] != 0 && _REGEX_CHECK_IS_W(text[i - 1]) == _REGEX_CHECK_IS_W(text[i]))
            {
                MRX_REWIND_OR_ABORT();
            }
        }
        else if(ctx->tokens[k].kind == REMIMU_KIND_NBOUND)
        {
            if(i == 0 && _REGEX_CHECK_IS_W(text[i]))
            {
                MRX_REWIND_OR_ABORT();
            }
            else if(i != 0 && text[i] == 0 && _REGEX_CHECK_IS_W(text[i - 1]))
            {
                MRX_REWIND_OR_ABORT();
            }
            else if(i != 0 && text[i] != 0 && _REGEX_CHECK_IS_W(text[i - 1]) != _REGEX_CHECK_IS_W(text[i]))
            {
                MRX_REWIND_OR_ABORT();
            }
        }
        else
        {
            /* deliberately unmatchable token (e.g. a{0}, a{0,0}) */
            if(ctx->tokens[k].count_hi == 1)
            {
                if(ctx->tokens[k].kind == REMIMU_KIND_OPEN || ctx->tokens[k].kind == REMIMU_KIND_NCOPEN)
                {
                    k += ctx->tokens[k].pair_offset;
                }
                else
                {
                    k += 1;
                }
                continue;
            }
            if(ctx->tokens[k].kind == REMIMU_KIND_OPEN || ctx->tokens[k].kind == REMIMU_KIND_NCOPEN)
            {
                if(!justrewinded)
                {
                    MRX_IFVERBOSE({ printf("hit OPEN. i is %ld, depth is %d\n", i, stackn); });
                    /*  need this to be able to detect and reject zero-size matches */
                    /* qgroupstate[ctx->tokens[k].mask[0]] = i; */

                    /*  ifwe're lazy and the min length is 0, we need to try the non-group case first */
                    if((ctx->tokens[k].mode & REMIMU_MODE_LAZY) && (ctx->tokens[k].count_lo == 0 || qgroupacceptszero[ctx->tokens[k + ctx->tokens[k].pair_offset].mask[0]]))
                    {
                        MRX_IFVERBOSE({ puts("trying non-group case first....."); });
                        range_min = 0;
                        range_max = 0;
                        MRX_REWIND_DO_SAVE(k);
                        k += ctx->tokens[k].pair_offset; /*  automatic += 1 will put us past the matching ) */
                    }
                    else
                    {
                        range_min = 1;
                        range_max = 0;
                        MRX_REWIND_DO_SAVE(k);
                    }
                }
                else
                {
                    MRX_IFVERBOSE({ printf("rewinded into OPEN. i is %ld, depth is %d\n", i, stackn); });
                    justrewinded = 0;
                    origk = k;
                    MRX_IFVERBOSE({ printf("--- trying to try another alternation, start k is %d, rmin is %ld\n", k, range_min); });
                    if(range_min != 0)
                    {
                        MRX_IFVERBOSE({ puts("rangemin is not zero. checking..."); });
                        k += range_min;
                        MRX_IFVERBOSE({ printf("start kind: %d\n", ctx->tokens[k].kind); });
                        MRX_IFVERBOSE({ printf("before start kind: %d\n", ctx->tokens[k - 1].kind); });
                        if(ctx->tokens[k - 1].kind == REMIMU_KIND_OR)
                        {
                            k += ctx->tokens[k - 1].pair_offset - 1;
                        }
                        else if(ctx->tokens[k - 1].kind == REMIMU_KIND_OPEN || ctx->tokens[k - 1].kind == REMIMU_KIND_NCOPEN)
                        {
                            k += ctx->tokens[k - 1].mask[15] - 1;
                        }
                        MRX_IFVERBOSE({ printf("kamakama %d %d\n", k, ctx->tokens[k].kind); });
                        if(ctx->tokens[k].kind == REMIMU_KIND_END) /*  unbalanced parens */
                        {
                            return -3;
                        }
                        MRX_IFVERBOSE({ printf("---?!?!   %d, %d\n", k, qgroupstate[ctx->tokens[k].mask[0]]); });
                        if(ctx->tokens[k].kind == REMIMU_KIND_CLOSE)
                        {
                            MRX_IFVERBOSE({ puts("!!~!~!~~~~!!~~!~   hit CLOSE. rewinding"); });
                            /*  do nothing and continue on ifwe don't need this group */
                            if(ctx->tokens[k].count_lo == 0 || qgroupacceptszero[ctx->tokens[k].mask[0]])
                            {
                                MRX_IFVERBOSE({ puts("continuing because we don't need this group"); });
                                qgroupstate[ctx->tokens[k].mask[0]] = 0;
                                if(!(ctx->tokens[k].mode & REMIMU_MODE_LAZY))
                                {
                                    qgroupstack[ctx->tokens[k].mask[0]] = 0;
                                }
                                continue;
                            }
                            /*  otherwise go to the last point before the group */
                            else
                            {
                                MRX_IFVERBOSE({ puts("going to last point before this group"); });
                                MRX_REWIND_OR_ABORT();
                                continue;
                            }
                        }

                        assert(ctx->tokens[k].kind == REMIMU_KIND_OR);
                    }
                    MRX_IFVERBOSE({ printf("--- FOUND ALTERNATION forparen at k %ld at k %d\n", origk, k); });
                    kdiff = k - origk;
                    range_min = kdiff + 1;
                    MRX_IFVERBOSE({ puts("(saving in paren after rewinding and looking fornext regex token to check)"); });
                    MRX_IFVERBOSE({ printf("%ld\n", range_min); });
                    MRX_REWIND_DO_SAVE(k - kdiff);
                }
            }
            else if(ctx->tokens[k].kind == REMIMU_KIND_CLOSE)
            {
                /*  unquantified */
                if(ctx->tokens[k].count_lo == 1 && ctx->tokens[k].count_hi == 2)
                {
                    /*  forcaptures */
                    capindex = qgroupcapindex[ctx->tokens[k].mask[0]];
                    if(capindex != 0xFFFF)
                    {
                        MRX_REWIND_DO_SAVE_DUMMY(k);
                    }
                }
                /*  quantified */
                else
                {
                    MRX_IFVERBOSE({ puts("closer test....."); });
                    if(!justrewinded)
                    {
                        prev = qgroupstack[ctx->tokens[k].mask[0]];

                        MRX_IFVERBOSE({
                            printf("qrqrqrqrqrqrqrq-------      k %d, gs %d, gaz %d, i %ld, tklo %d, rmin %ld, tkhi %d, rmax %ld, prev %d, sn %d\n", k,
                                   qgroupstate[ctx->tokens[k].mask[0]], qgroupacceptszero[ctx->tokens[k].mask[0]], i, ctx->tokens[k].count_lo, range_min, ctx->tokens[k].count_hi,
                                   range_max, prev, stackn);
                        });

                        range_max = ctx->tokens[k].count_hi;
                        range_max -= 1;
                        range_min = qgroupacceptszero[ctx->tokens[k].mask[0]] ? 0 : ctx->tokens[k].count_lo;
                        /* assert(qgroupstate[ctx->tokens[k + ctx->tokens[k].pair_offset].mask[0]] <= i); */
                        /* if(prev) assert(rewindstack[prev].i <= i); */
                        MRX_IFVERBOSE({ printf("qzqzqzqzqzqzqzq-------      rmin %ld, rmax %ld\n", range_min, range_max); });
                        /*  minimum requirement not yet met */
                        if(qgroupstate[ctx->tokens[k].mask[0]] + 1 < range_min)
                        {
                            MRX_IFVERBOSE({ puts("continuing minimum matches fora quantified group"); });
                            qgroupstate[ctx->tokens[k].mask[0]] += 1;
                            MRX_REWIND_DO_SAVE(k);
                            k += ctx->tokens[k].pair_offset; /*  back to start of group */
                            k -= 1; /*  ensure we actually hit the group node next and not the node after it */
                            continue;
                        }
                        /*  maximum allowance exceeded */
                        else if(ctx->tokens[k].count_hi != 0 && qgroupstate[ctx->tokens[k].mask[0]] + 1 > range_max)
                        {
                            MRX_IFVERBOSE({ printf("hit maximum allowed instances of a quantified group %d %ld\n", qgroupstate[ctx->tokens[k].mask[0]], range_max); });
                            range_max -= 1;
                            MRX_REWIND_OR_ABORT();
                            continue;
                        }

                        /*  fallback case to detect zero-length matches when we backtracked into the inside of this group */
                        /*  after an attempted parse of a second copy of itself */
                        forcezero = 0;
                        if(prev != 0 && (uint32_t)rewindstack[prev].i > (uint32_t)i)
                        {
                            /*  find matching open paren */
                            n = stackn - 1;
                            while(n > 0 && rewindstack[n].k != k + ctx->tokens[k].pair_offset)
                            {
                                n -= 1;
                            }
                            assert(n > 0);
                            if(rewindstack[n].i == i)
                            {
                                forcezero = 1;
                            }
                        }

                        /*  reject zero-length matches */
                        if((forcezero || (prev != 0 && (uint32_t)rewindstack[prev].i == (uint32_t)i))) /*   && qgroupstate[ctx->tokens[k].mask[0]] > 0 */
                        {
                            MRX_IFVERBOSE({ printf("rejecting zero-length match..... %d %ld %ld\n", forcezero, rewindstack[prev].i, i); });
                            MRX_IFVERBOSE({ printf("%d (k: %d)\n", qgroupstate[ctx->tokens[k].mask[0]], k); });
                            qgroupacceptszero[ctx->tokens[k].mask[0]] = 1;
                            MRX_REWIND_OR_ABORT();
                            /* range_max = qgroupstate[ctx->tokens[k].mask[0]]; */
                            /* range_min = 0; */
                        }
                        else if(ctx->tokens[k].mode & REMIMU_MODE_LAZY) /*  lazy */
                        {
                            MRX_IFVERBOSE(
                            { printf("nidnfasidfnidfndifn-------      %d, %d, %ld\n", qgroupstate[ctx->tokens[k].mask[0]], ctx->tokens[k].count_lo, range_min); });
                            if(prev)
                            {
                                MRX_IFVERBOSE(
                                { printf("lazy doesn't think it's zero-length. prev i %ld vs i %ld (depth %d)\n", rewindstack[prev].i, i, stackn); });
                            }
                            /*  continue on to past the group; group retry is in rewind state */
                            qgroupstate[ctx->tokens[k].mask[0]] += 1;
                            MRX_REWIND_DO_SAVE(k);
                            qgroupstate[ctx->tokens[k].mask[0]] = 0;
                        }
                        else /*  greedy */
                        {
                            MRX_IFVERBOSE({ puts("wahiwahi"); });
                            /*  clear unwanted memory ifpossessive */
                            if((ctx->tokens[k].mode & REMIMU_MODE_POSSESSIVE))
                            {
                                k2 = k;
                                /*  special case forfirst, only rewind to (, not to ) */
                                if(qgroupstate[ctx->tokens[k].mask[0]] == 0)
                                {
                                    k2 = k + ctx->tokens[k].pair_offset;
                                }
                                if(stackn == 0)
                                {
                                    return -1;
                                }
                                stackn -= 1;
                                while(stackn > 0 && rewindstack[stackn].k != k2)
                                {
                                    stackn -= 1;
                                }
                                if(stackn == 0)
                                {
                                    return -1;
                                }
                            }
                            /*  continue to next match ifsane */
                            if((uint32_t)qgroupstate[ctx->tokens[k + ctx->tokens[k].pair_offset].mask[0]] < (uint32_t)i)
                            {
                                MRX_IFVERBOSE({ puts("REWINDING FROM GREEDY NON-REWIND CLOSER"); });
                                qgroupstate[ctx->tokens[k].mask[0]] += 1;
                                MRX_REWIND_DO_SAVE(k);
                                k += ctx->tokens[k].pair_offset; /*  back to start of group */
                                k -= 1; /*  ensure we actually hit the group node next and not the node after it */
                            }
                            else
                            {
                                MRX_IFVERBOSE({ puts("CONTINUING FROM GREEDY NON-REWIND CLOSER"); });
                            }
                        }
                    }
                    else
                    {
                        MRX_IFVERBOSE({ puts("IN CLOSER REWIND!!!"); });
                        justrewinded = 0;

                        if(ctx->tokens[k].mode & REMIMU_MODE_LAZY)
                        {
                            /*  lazy rewind: need to try matching the group again */
                            MRX_REWIND_DO_SAVE_DUMMY(k);
                            qgroupstack[ctx->tokens[k].mask[0]] = stackn;
                            k += ctx->tokens[k].pair_offset; /*  back to start of group */
                            k -= 1; /*  ensure we actually hit the group node next and not the node after it */
                        }
                        else
                        {
                            /*  greedy. ifwe're going to go outside the acceptable range, rewind */
                            MRX_IFVERBOSE({ printf("kufukufu %d %ld\n", ctx->tokens[k].count_lo, range_min); });
                            /* uint64_t oldi = i; */
                            if(qgroupstate[ctx->tokens[k].mask[0]] < range_min && !qgroupacceptszero[ctx->tokens[k].mask[0]])
                            {
                                MRX_IFVERBOSE({
                                    printf("rewinding from greedy group because we're going to go out of range (%d vs %ld)\n", qgroupstate[ctx->tokens[k].mask[0]], range_min);
                                });
                                /* i = oldi; */
                                MRX_REWIND_OR_ABORT();
                            }
                            /*  otherwise continue on to past the group */
                            else
                            {
                                MRX_IFVERBOSE({ puts("continuing past greedy group"); });
                                qgroupstate[ctx->tokens[k].mask[0]] = 0;
                                /*  forcaptures */
                                capindex = qgroupcapindex[ctx->tokens[k].mask[0]];
                                if(capindex != 0xFFFF)
                                {
                                    MRX_REWIND_DO_SAVE_DUMMY(k);
                                }
                            }
                        }
                    }
                }
            }
            else if(ctx->tokens[k].kind == REMIMU_KIND_OR)
            {
                MRX_IFVERBOSE({ printf("hit OR at %d. adding %d\n", k, ctx->tokens[k].pair_offset); });
                k += ctx->tokens[k].pair_offset;
                k -= 1;
            }
            else if(ctx->tokens[k].kind == REMIMU_KIND_NORMAL)
            {
                if(!justrewinded)
                {
                    ntcnt = 0;
                    /*  do whatever the obligatory minimum amount of matching is */
                    oldi = i;
                    while(ntcnt < ctx->tokens[k].count_lo && text[i] != 0 && _REGEX_CHECK_MASK(ctx->tokens, k, text[i]))
                    {
                        i += 1;
                        ntcnt += 1;
                    }
                    if(ntcnt < ctx->tokens[k].count_lo)
                    {
                        MRX_IFVERBOSE({ printf("non-match A. rewinding (token %d)\n", k); });
                        i = oldi;
                        MRX_REWIND_OR_ABORT();
                        continue;
                    }
                    if(ctx->tokens[k].mode & REMIMU_MODE_LAZY)
                    {
                        range_min = ntcnt;
                        range_max = ctx->tokens[k].count_hi - 1;
                        MRX_REWIND_DO_SAVE(k);
                    }
                    else
                    {
                        hiclimit = ctx->tokens[k].count_hi;
                        if(hiclimit == 0)
                        {
                            hiclimit = ~hiclimit;
                        }
                        range_min = ntcnt;
                        while(text[i] != 0 && _REGEX_CHECK_MASK(ctx->tokens, k, text[i]) && ntcnt + 1 < hiclimit)
                        {
                            MRX_IFVERBOSE({ printf("match!! (%c)\n", text[i]); });
                            i += 1;
                            ntcnt += 1;
                        }
                        range_max = ntcnt;
                        MRX_IFVERBOSE({ printf("set rmin to %ld and rmax to %ld on entry into normal greedy token with k %d\n", range_min, range_max, k); });
                        if(!(ctx->tokens[k].mode & REMIMU_MODE_POSSESSIVE))
                        {
                            MRX_REWIND_DO_SAVE(k);
                        }
                    }
                }
                else
                {
                    justrewinded = 0;
                    if(ctx->tokens[k].mode & REMIMU_MODE_LAZY)
                    {
                        rangelimit = range_max;
                        if(rangelimit == 0)
                        {
                            rangelimit = ~rangelimit;
                        }
                        if(_REGEX_CHECK_MASK(ctx->tokens, k, text[i]) && text[i] != 0 && range_min < rangelimit)
                        {
                            MRX_IFVERBOSE({ printf("match2!! (%c) (k: %d)\n", text[i], k); });
                            i += 1;
                            range_min += 1;
                            MRX_REWIND_DO_SAVE(k);
                        }
                        else
                        {
                            MRX_IFVERBOSE({ printf("core rewind lazy (k: %d)\n", k); });
                            MRX_REWIND_OR_ABORT();
                        }
                    }
                    else
                    {
                        /*
                        MRX_IFVERBOSE({
                            printf("comparing rmin %d and rmax %d token with k %d\n", range_min, range_max, k);
                        });
                        */
                        if(range_max > range_min)
                        {
                            MRX_IFVERBOSE({ printf("greedy normal going back (k: %d)\n", k); });
                            i -= 1;
                            range_max -= 1;
                            MRX_REWIND_DO_SAVE(k);
                        }
                        else
                        {
                            MRX_IFVERBOSE({ printf("core rewind greedy (k: %d)\n", k); });
                            MRX_REWIND_OR_ABORT();
                        }
                    }
                }
            }
            else
            {
                fprintf(stderr, "unimplemented token kind %d\n", ctx->tokens[k].kind);
                assert(0);
            }
        }
        /*
        printf("k... %d\n", k);
        */
    }
    if(caps != 0)
    {
        /*
        printf("stackn: %d\n", stackn);
        */
        fflush(stdout);
        for(n = 0; n < stackn; n++)
        {
            RegexMatcherState s = rewindstack[n];
            kind = ctx->tokens[s.k].kind;
            if(kind == REMIMU_KIND_OPEN || kind == REMIMU_KIND_CLOSE)
            {
                capindex = qgroupcapindex[ctx->tokens[s.k].mask[0]];
                if(capindex == 0xFFFF)
                {
                    continue;
                }
                if(ctx->tokens[s.k].kind == REMIMU_KIND_OPEN)
                {
                    cappos[capindex] = s.i;
                }
                else if(cappos[capindex] >= 0)
                {
                    capspan[capindex] = s.i - cappos[capindex];
                }
            }
        }
        /*  re-deinitialize capture positions that have no associated capture span */
        for(n = 0; n < caps; n++)
        {
            if(capspan[n] == -1)
            {
                cappos[n] = -1;
            }
        }
    }
    return i;
}

#undef MRX_REWIND_DO_SAVE
#undef MRX_REWIND_OR_ABORT
#undef _REGEX_CHECK_IS_W
#undef _P_TEXT_HIGHLIGHTED
#undef MRX_IFVERBOSE

#define _PRINT_C_SMART(c) \
    { \
        if(c >= 0x20 && c <= 0x7E) \
        { \
            printf("%c", c); \
        } \
        else \
        { \
            printf("\\x%02x", c); \
        } \
    }

REMIMU_INLINE void mrx_regex_printtokens(RegexToken* tokens)
{
    int c;
    int k;
    int cold;
    static const char* kindtostr[] = {
        "NORMAL", "OPEN", "NCOPEN", "CLOSE", "OR", "CARET", "DOLLAR", "BOUND", "NBOUND", "END",
    };
    static const char* modetostr[] = {
        "GREEDY",
        "POSSESS",
        "LAZY",
    };
    for(k = 0;; k++)
    {
        printf("%s\t%s\t", kindtostr[tokens[k].kind], modetostr[tokens[k].mode]);
        cold = -1;
        for(c = 0; c < (tokens[k].kind ? 0 : 256); c++)
        {
            if(_REGEX_CHECK_MASK(tokens, k, c))
            {
                if(cold == -1)
                {
                    cold = c;
                }
            }
            else if(cold != -1)
            {
                if(c - 1 == cold)
                {
                    _PRINT_C_SMART(cold)
                    cold = -1;
                }
                else if(c - 2 == cold)
                {
                    _PRINT_C_SMART(cold)
                    _PRINT_C_SMART(cold + 1)
                    cold = -1;
                }
                else
                {
                    _PRINT_C_SMART(cold)
                    printf("-");
                    _PRINT_C_SMART(c - 1)
                    cold = -1;
                }
            }
        }
        /*
        printf("\t");
        for(int i = 0; i < 16; i++)
            printf("%04x", tokens[k].mask[i]);
        */
        printf("\t{%d,%d}\t(%d)\n", tokens[k].count_lo, tokens[k].count_hi - 1, tokens[k].pair_offset);
        if(tokens[k].kind == REMIMU_KIND_END)
        {
            break;
        }
    }
}

#undef _REGEX_CHECK_MASK
