
#include "neon.h"
#include "mrx.h"
#include "strbuf.h"

/*
* TODO: get rid of unused functions
*/

NNObjString* nn_string_makefromstrbuf(NNState* state, NNStringBuffer* buf, uint32_t hsv)
{
    NNObjString* rs;
    rs = (NNObjString*)nn_object_allocobject(state, sizeof(NNObjString), NEON_OBJTYPE_STRING, false);
    rs->sbuf = *buf;
    rs->hashvalue = hsv;
    nn_vm_stackpush(state, nn_value_fromobject(rs));
    nn_valtable_set(&state->allocatedstrings, nn_value_fromobject(rs), nn_value_makenull());
    nn_vm_stackpop(state);
    return rs;
}

void nn_string_destroy(NNState* state, NNObjString* str)
{
    nn_strbuf_destroyfromstack(&str->sbuf);
    nn_gcmem_release(state, str, sizeof(NNObjString));
}

#if 0
NNObjString* nn_string_internlen(NNState* state, const char* chars, int length)
{
    uint32_t hsv;
    NNStringBuffer buf;
    hsv = nn_util_hashstring(chars, length);
    nn_strbuf_makebasicemptystack(&buf, length);
    nn_strbuf_setdata(&buf, (char*)chars);
    nn_strbuf_setlength(&buf, length);
    buf.isintern = true;
    return nn_string_makefromstrbuf(state, &buf, hsv);
}

#else

NNObjString* nn_string_internlen(NNState* state, const char* chars, int length)
{
    return nn_string_copylen(state, chars, length);
}
    
#endif

NNObjString* nn_string_intern(NNState* state, const char* chars)
{
    return nn_string_internlen(state, chars, strlen(chars));
}

NNObjString* nn_string_takelen(NNState* state, char* chars, int length)
{
    uint32_t hsv;
    NNObjString* rs;
    NNStringBuffer buf;
    hsv = nn_util_hashstring(chars, length);
    rs = nn_valtable_findstring(&state->allocatedstrings, chars, length, hsv);
    if(rs != NULL)
    {
        nn_memory_free(chars);
        return rs;
    }
    nn_strbuf_makebasicemptystack(&buf, length);
    nn_strbuf_setdata(&buf, chars);
    nn_strbuf_setlength(&buf, length);
    return nn_string_makefromstrbuf(state, &buf, hsv);
}

NNObjString* nn_string_takecstr(NNState* state, char* chars)
{
    return nn_string_takelen(state, chars, strlen(chars));
}

NNObjString* nn_string_copylen(NNState* state, const char* chars, int length)
{
    uint32_t hsv;
    NNStringBuffer buf;
    NNObjString* rs;
    hsv = nn_util_hashstring(chars, length);
    rs = nn_valtable_findstring(&state->allocatedstrings, chars, length, hsv);
    if(rs != NULL)
    {
        return rs;
    }
    nn_strbuf_makebasicemptystack(&buf, length);
    nn_strbuf_appendstrn(&buf, chars, length);
    rs = nn_string_makefromstrbuf(state, &buf, hsv);
    return rs;
}

NNObjString* nn_string_copycstr(NNState* state, const char* chars)
{
    return nn_string_copylen(state, chars, strlen(chars));
}

NNObjString* nn_string_copyobject(NNState* state, NNObjString* origos)
{
    return nn_string_copylen(state, nn_string_getdata(origos), nn_string_getlength(origos));
}

const char* nn_string_getdata(NNObjString* os)
{
    return nn_strbuf_data(&os->sbuf);
}

char* nn_string_mutdata(NNObjString* os)
{
    return nn_strbuf_mutdata(&os->sbuf);
}

size_t nn_string_getlength(NNObjString* os)
{
    return nn_strbuf_length(&os->sbuf);
}

bool nn_string_setlength(NNObjString* os, size_t nlen)
{
    return nn_strbuf_setlength(&os->sbuf, nlen);
}

bool nn_string_set(NNObjString* os, size_t idx, int byte)
{
    return nn_strbuf_set(&os->sbuf, idx, byte);
}

int nn_string_get(NNObjString* os, size_t idx)
{
    return nn_strbuf_get(&os->sbuf, idx);
}

bool nn_string_appendstringlen(NNObjString* os, const char* str, size_t len)
{
    return nn_strbuf_appendstrn(&os->sbuf, str, len);
}

bool nn_string_appendstring(NNObjString* os, const char* str)
{
    return nn_string_appendstringlen(os, str, strlen(str));
}

bool nn_string_appendobject(NNObjString* os, NNObjString* other)
{
    return nn_string_appendstringlen(os, nn_string_getdata(other), nn_string_getlength(other));
}

bool nn_string_appendbyte(NNObjString* os, int ch)
{
    return nn_strbuf_appendchar(&os->sbuf, ch);
}

bool nn_string_appendnumulong(NNObjString* os, unsigned long val)
{
    return nn_strbuf_appendnumulong(&os->sbuf, val);
}

bool nn_string_appendnumint(NNObjString* os, int val)
{
    return nn_strbuf_appendnumint(&os->sbuf, val);
}

int nn_string_appendfmtv(NNObjString* os, const char* fmt, va_list va)
{
    return nn_strbuf_appendformatv(&os->sbuf, fmt, va);
}

int nn_string_appendfmt(NNObjString* os, const char* fmt, ...)
{
    int r;
    va_list va;
    va_start(va, fmt);
    r = nn_string_appendfmtv(os, fmt, va);
    va_end(va);
    return r;
}

NNObjString* nn_string_substrlen(NNObjString* os, size_t start, size_t maxlen)
{
    char* str;
    NNObjString* rt;
    str = nn_strbuf_substr(&os->sbuf, start, maxlen);
    rt = nn_string_takelen(((NNObject*)os)->pstate, str, maxlen);
    return rt;
}

NNObjString* nn_string_substr(NNObjString* os, size_t start)
{
    return nn_string_substrlen(os, start, nn_string_getlength(os));
}


NNValue nn_objfnstring_utf8numbytes(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    int incode;
    int res;
    NNArgCheck check;
    (void)thisval;
    nn_argcheck_init(state, &check, "utf8NumBytes", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isnumber);
    incode = nn_value_asnumber(argv[0]);
    res = nn_util_utf8numbytes(incode);
    return nn_value_makenumber(res);
}

NNValue nn_objfnstring_utf8decode(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    int res;
    NNObjString* instr;
    NNArgCheck check;
    (void)thisval;
    nn_argcheck_init(state, &check, "utf8Decode", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isstring);
    instr = nn_value_asstring(argv[0]);
    res = nn_util_utf8decode((const uint8_t*)nn_string_getdata(instr), nn_string_getlength(instr));
    return nn_value_makenumber(res);
}

NNValue nn_objfnstring_utf8encode(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    int incode;
    size_t len;
    NNObjString* res;
    char* buf;
    NNArgCheck check;
    (void)thisval;
    nn_argcheck_init(state, &check, "utf8Encode", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isnumber);
    incode = nn_value_asnumber(argv[0]);
    buf = nn_util_utf8encode(incode, &len);
    res = nn_string_takelen(state, buf, len);
    return nn_value_fromobject(res);
}

static NNValue nn_util_stringutf8chars(NNState* state, NNValue thisval, NNValue* argv, size_t argc, bool onlycodepoint)
{
    int cp;
    bool havemax;
    size_t counter;
    size_t maxamount;
    const char* cstr;
    NNObjArray* res;
    NNObjString* os;
    NNObjString* instr;
    utf8iterator_t iter;
    havemax = false;
    instr = nn_value_asstring(thisval);
    if(argc > 0)
    {
        havemax = true;
        maxamount = nn_value_asnumber(argv[0]);
    }
    res = nn_array_make(state);
    nn_utf8iter_init(&iter, nn_string_getdata(instr), nn_string_getlength (instr));
    counter = 0;
    while(nn_utf8iter_next(&iter))
    {
        cp = iter.codepoint;
        cstr = nn_utf8iter_getchar(&iter);
        counter++;
        if(havemax)
        {
            if(counter == maxamount)
            {
                goto finalize;
            }
        }
        if(onlycodepoint)
        {
            nn_array_push(res, nn_value_makenumber(cp));
        }
        else
        {
            os = nn_string_copylen(state, cstr, iter.charsize);
            nn_array_push(res, nn_value_fromobject(os));
        }
    }
    finalize:
    return nn_value_fromobject(res);
}

NNValue nn_objfnstring_utf8chars(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    return nn_util_stringutf8chars(state, thisval, argv, argc, false);
}

NNValue nn_objfnstring_utf8codepoints(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    return nn_util_stringutf8chars(state, thisval, argv, argc, true);
}


NNValue nn_objfnstring_fromcharcode(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    char ch;
    NNObjString* os;
    NNArgCheck check;
    (void)thisval;
    nn_argcheck_init(state, &check, "fromCharCode", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isnumber);
    ch = nn_value_asnumber(argv[0]);
    os = nn_string_copylen(state, &ch, 1);
    return nn_value_fromobject(os);
}

NNValue nn_objfnstring_constructor(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNObjString* os;
    NNArgCheck check;
    (void)thisval;
    nn_argcheck_init(state, &check, "constructor", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    os = nn_string_copylen(state, "", 0);
    return nn_value_fromobject(os);
}

NNValue nn_objfnstring_length(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNArgCheck check;
    NNObjString* selfstr;
    nn_argcheck_init(state, &check, "length", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    selfstr = nn_value_asstring(thisval);
    return nn_value_makenumber(nn_string_getlength(selfstr));
}

NNValue nn_string_fromrange(NNState* state, const char* buf, int len)
{
    NNObjString* str;
    if(len <= 0)
    {
        return nn_value_fromobject(nn_string_copylen(state, "", 0));
    }
    str = nn_string_copylen(state, "", 0);
    nn_string_appendstringlen(str, buf, len);
    return nn_value_fromobject(str);
}

NNObjString* nn_string_substring(NNState* state, NNObjString* selfstr, size_t start, size_t end, bool likejs)
{
    size_t asz;
    size_t len;
    size_t tmp;
    size_t maxlen;
    char* raw;
    (void)likejs;
    maxlen = nn_string_getlength(selfstr);
    len = maxlen;
    if(end > maxlen)
    {
        tmp = start;
        start = end;
        end = tmp;
        len = maxlen;
    }
    if(end < start)
    {
        tmp = end;
        end = start;
        start = tmp;
        len = end;
    }
    len = (end - start);
    if(len > maxlen)
    {
        len = maxlen;
    }
    asz = ((end + 1) * sizeof(char));
    raw = (char*)nn_memory_malloc(sizeof(char) * asz);
    memset(raw, 0, asz);
    memcpy(raw, nn_string_getdata(selfstr) + start, len);
    return nn_string_takelen(state, raw, len);
}

NNValue nn_objfnstring_substring(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    size_t end;
    size_t start;
    size_t maxlen;
    NNObjString* nos;
    NNObjString* selfstr;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "substring", argv, argc);
    selfstr = nn_value_asstring(thisval);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isnumber);
    maxlen = nn_string_getlength(selfstr);
    end = maxlen;
    start = nn_value_asnumber(argv[0]);
    if(argc > 1)
    {
        NEON_ARGS_CHECKTYPE(&check, 1, nn_value_isnumber);
        end = nn_value_asnumber(argv[1]);
    }
    nos = nn_string_substring(state, selfstr, start, end, true);
    return nn_value_fromobject(nos);
}

NNValue nn_objfnstring_charcodeat(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    int ch;
    int idx;
    int selflen;
    NNObjString* selfstr;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "charCodeAt", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isnumber);
    selfstr = nn_value_asstring(thisval);
    idx = nn_value_asnumber(argv[0]);
    selflen = (int)nn_string_getlength(selfstr);
    if((idx < 0) || (idx >= selflen))
    {
        ch = -1;
    }
    else
    {
        ch = nn_string_get(selfstr, idx);
    }
    return nn_value_makenumber(ch);
}

NNValue nn_objfnstring_charat(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    char ch;
    int idx;
    int selflen;
    NNObjString* selfstr;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "charAt", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isnumber);
    selfstr = nn_value_asstring(thisval);
    idx = nn_value_asnumber(argv[0]);
    selflen = (int)nn_string_getlength(selfstr);
    if((idx < 0) || (idx >= selflen))
    {
        return nn_value_fromobject(nn_string_copylen(state, "", 0));
    }
    else
    {
        ch = nn_string_get(selfstr, idx);
    }
    return nn_value_fromobject(nn_string_copylen(state, &ch, 1));
}

NNValue nn_objfnstring_upper(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    size_t slen;
    char* string;
    NNObjString* str;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "upper", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    str = nn_value_asstring(thisval);
    slen = nn_string_getlength(str);
    string = nn_util_strtoupper(nn_string_mutdata(str), slen);
    return nn_value_fromobject(nn_string_copylen(state, string, slen));
}

NNValue nn_objfnstring_lower(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    size_t slen;
    char* string;
    NNObjString* str;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "lower", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    str = nn_value_asstring(thisval);
    slen = nn_string_getlength(str);
    string = nn_util_strtolower(nn_string_mutdata(str), slen);
    return nn_value_fromobject(nn_string_copylen(state, string, slen));
}

NNValue nn_objfnstring_isalpha(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    size_t i;
    size_t len;
    NNArgCheck check;
    NNObjString* selfstr;
    nn_argcheck_init(state, &check, "isAlpha", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    selfstr = nn_value_asstring(thisval);
    len = nn_string_getlength(selfstr);
    for(i = 0; i < len; i++)
    {
        if(!isalpha((unsigned char)nn_string_get(selfstr, i)))
        {
            return nn_value_makebool(false);
        }
    }
    return nn_value_makebool(nn_string_getlength(selfstr) != 0);
}

NNValue nn_objfnstring_isalnum(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    size_t i;
    size_t len;
    NNObjString* selfstr;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "isAlnum", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    selfstr = nn_value_asstring(thisval);
    len = nn_string_getlength(selfstr);
    for(i = 0; i < len; i++)
    {
        if(!isalnum((unsigned char)nn_string_get(selfstr, i)))
        {
            return nn_value_makebool(false);
        }
    }
    return nn_value_makebool(nn_string_getlength(selfstr) != 0);
}

NNValue nn_objfnstring_isfloat(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    double f;
    char* p;
    NNObjString* selfstr;
    NNArgCheck check;
    (void)f;
    nn_argcheck_init(state, &check, "isFloat", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    selfstr = nn_value_asstring(thisval);
    errno = 0;
    if(nn_string_getlength(selfstr) ==0)
    {
        return nn_value_makebool(false);
    }
    f = strtod(nn_string_getdata(selfstr), &p);
    if(errno)
    {
        return nn_value_makebool(false);
    }
    else
    {
        if(*p == 0)
        {
            return nn_value_makebool(true);
        }
    }
    return nn_value_makebool(false);
}

NNValue nn_objfnstring_isnumber(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    size_t i;
    size_t len;
    NNObjString* selfstr;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "isNumber", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    selfstr = nn_value_asstring(thisval);
    len = nn_string_getlength(selfstr);
    for(i = 0; i < len; i++)
    {
        if(!isdigit((unsigned char)nn_string_get(selfstr, i)))
        {
            return nn_value_makebool(false);
        }
    }
    return nn_value_makebool(nn_string_getlength(selfstr) != 0);
}

NNValue nn_objfnstring_islower(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    size_t i;
    size_t len;
    bool alphafound;
    NNObjString* selfstr;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "isLower", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    selfstr = nn_value_asstring(thisval);
    alphafound = false;
    len = nn_string_getlength(selfstr);
    for(i = 0; i < len; i++)
    {
        if(!alphafound && !isdigit(nn_string_get(selfstr, 0)))
        {
            alphafound = true;
        }
        if(isupper(nn_string_get(selfstr, 0)))
        {
            return nn_value_makebool(false);
        }
    }
    return nn_value_makebool(alphafound);
}

NNValue nn_objfnstring_isupper(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    size_t i;
    size_t len;
    bool alphafound;
    NNObjString* selfstr;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "isUpper", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    selfstr = nn_value_asstring(thisval);
    alphafound = false;
    len = nn_string_getlength(selfstr);
    for(i = 0; i < len; i++)
    {
        if(!alphafound && !isdigit(nn_string_get(selfstr, 0)))
        {
            alphafound = true;
        }
        if(islower(nn_string_get(selfstr, i)))
        {
            return nn_value_makebool(false);
        }
    }
    return nn_value_makebool(alphafound);
}

NNValue nn_objfnstring_isspace(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    size_t i;
    size_t len;
    NNObjString* selfstr;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "isSpace", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    selfstr = nn_value_asstring(thisval);
    len = nn_string_getlength(selfstr);
    for(i = 0; i < len; i++)
    {
        if(!isspace((unsigned char)nn_string_get(selfstr, i)))
        {
            return nn_value_makebool(false);
        }
    }
    return nn_value_makebool(nn_string_getlength(selfstr) != 0);
}

NNValue nn_objfnstring_trim(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    char trimmer;
    char* end;
    char* string;
    NNObjString* selfstr;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "trim", argv, argc);
    NEON_ARGS_CHECKCOUNTRANGE(&check, 0, 1);
    trimmer = '\0';
    if(argc == 1)
    {
        trimmer = (char)nn_string_get(nn_value_asstring(argv[0]), 0);
    }
    selfstr = nn_value_asstring(thisval);
    string = nn_string_mutdata(selfstr);
    end = NULL;
    /* Trim leading space*/
    if(trimmer == '\0')
    {
        while(isspace((unsigned char)*string))
        {
            string++;
        }
    }
    else
    {
        while(trimmer == *string)
        {
            string++;
        }
    }
    /* All spaces? */
    if(*string == 0)
    {
        return nn_value_fromobject(nn_string_copylen(state, "", 0));
    }
    /* Trim trailing space */
    end = string + strlen(string) - 1;
    if(trimmer == '\0')
    {
        while(end > string && isspace((unsigned char)*end))
        {
            end--;
        }
    }
    else
    {
        while(end > string && trimmer == *end)
        {
            end--;
        }
    }
    end[1] = '\0';
    return nn_value_fromobject(nn_string_copycstr(state, string));
}

NNValue nn_objfnstring_ltrim(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    char trimmer;
    char* end;
    char* string;
    NNObjString* selfstr;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "ltrim", argv, argc);
    NEON_ARGS_CHECKCOUNTRANGE(&check, 0, 1);
    trimmer = '\0';
    if(argc == 1)
    {
        trimmer = (char)nn_string_get(nn_value_asstring(argv[0]), 0);
    }
    selfstr = nn_value_asstring(thisval);
    string = nn_string_mutdata(selfstr);
    end = NULL;
    /* Trim leading space */
    if(trimmer == '\0')
    {
        while(isspace((unsigned char)*string))
        {
            string++;
        }
    }
    else
    {
        while(trimmer == *string)
        {
            string++;
        }
    }
    /* All spaces? */
    if(*string == 0)
    {
        return nn_value_fromobject(nn_string_copylen(state, "", 0));
    }
    end = string + strlen(string) - 1;
    end[1] = '\0';
    return nn_value_fromobject(nn_string_copycstr(state, string));
}

NNValue nn_objfnstring_rtrim(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    char trimmer;
    char* end;
    char* string;
    NNObjString* selfstr;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "rtrim", argv, argc);
    NEON_ARGS_CHECKCOUNTRANGE(&check, 0, 1);
    trimmer = '\0';
    if(argc == 1)
    {
        trimmer = (char)nn_string_get(nn_value_asstring(argv[0]), 0);
    }
    selfstr = nn_value_asstring(thisval);
    string = nn_string_mutdata(selfstr);
    end = NULL;
    /* All spaces? */
    if(*string == 0)
    {
        return nn_value_fromobject(nn_string_copylen(state, "", 0));
    }
    end = string + strlen(string) - 1;
    if(trimmer == '\0')
    {
        while(end > string && isspace((unsigned char)*end))
        {
            end--;
        }
    }
    else
    {
        while(end > string && trimmer == *end)
        {
            end--;
        }
    }
    /* Write new null terminator character */
    end[1] = '\0';
    return nn_value_fromobject(nn_string_copycstr(state, string));
}

NNValue nn_objfnstring_indexof(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    int startindex;
    char* result;
    const char* haystack;
    NNObjString* string;
    NNObjString* needle;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "indexOf", argv, argc);
    NEON_ARGS_CHECKCOUNTRANGE(&check, 1, 2);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isstring);
    string = nn_value_asstring(thisval);
    needle = nn_value_asstring(argv[0]);
    startindex = 0;
    if(argc == 2)
    {
        NEON_ARGS_CHECKTYPE(&check, 1, nn_value_isnumber);
        startindex = nn_value_asnumber(argv[1]);
    }
    if(nn_string_getlength(string) > 0 && nn_string_getlength(needle) > 0)
    {
        haystack = nn_string_getdata(string);
        result = strstr(haystack + startindex, nn_string_getdata(needle));
        if(result != NULL)
        {
            return nn_value_makenumber((int)(result - haystack));
        }
    }
    return nn_value_makenumber(-1);
}

NNValue nn_objfnstring_startswith(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNObjString* substr;
    NNObjString* string;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "startsWith", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isstring);
    string = nn_value_asstring(thisval);
    substr = nn_value_asstring(argv[0]);
    if(nn_string_getlength(string) == 0 || nn_string_getlength(substr) == 0 || nn_string_getlength(substr) > nn_string_getlength(string))
    {
        return nn_value_makebool(false);
    }
    return nn_value_makebool(memcmp(nn_string_getdata(substr), nn_string_getdata(string), nn_string_getlength(substr)) == 0);
}

NNValue nn_objfnstring_endswith(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    int difference;
    NNObjString* substr;
    NNObjString* string;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "endsWith", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isstring);
    string = nn_value_asstring(thisval);
    substr = nn_value_asstring(argv[0]);
    if(nn_string_getlength(string) == 0 || nn_string_getlength(substr) == 0 || nn_string_getlength(substr) > nn_string_getlength(string))
    {
        return nn_value_makebool(false);
    }
    difference = nn_string_getlength(string) - nn_string_getlength(substr);
    return nn_value_makebool(memcmp(nn_string_getdata(substr), nn_string_getdata(string) + difference, nn_string_getlength(substr)) == 0);
}

NNValue nn_util_stringregexmatch(NNState* state, NNObjString* string, NNObjString* pattern, bool capture)
{
    enum {
        matchMaxTokens = 128*4,
        matchMaxCaptures = 128,
    };
    int prc;
    int64_t i;
    int64_t mtstart;
    int64_t mtlength;
    int64_t actualmaxcaptures;
    int64_t cpres;
    int16_t restokens;
    int64_t capstarts[matchMaxCaptures + 1];
    int64_t caplengths[matchMaxCaptures + 1];
    RegexToken tokens[matchMaxTokens + 1];
    RegexContext pctx;
    memset(tokens, 0, (matchMaxTokens+1) * sizeof(RegexToken));
    memset(caplengths, 0, (matchMaxCaptures + 1) * sizeof(int64_t));
    memset(capstarts, 0, (matchMaxCaptures + 1) * sizeof(int64_t));
    const char* strstart;
    NNObjString* rstr;
    NNObjArray* oa;
    NNObjDict* dm;
    restokens = matchMaxTokens;
    actualmaxcaptures = 0;
    mrx_context_initstack(&pctx, tokens, restokens);
    if(capture)
    {
        actualmaxcaptures = matchMaxCaptures;
    }
    prc = mrx_regex_parse(&pctx, nn_string_getdata(pattern), 0);
    if(prc == 0)
    {
        cpres = mrx_regex_match(&pctx, nn_string_getdata(string), 0, actualmaxcaptures, capstarts, caplengths);
        if(cpres > 0)
        {
            if(capture)
            {
                oa = nn_object_makearray(state);
                for(i=0; i<cpres; i++)
                {
                    mtstart = capstarts[i];
                    mtlength = caplengths[i];
                    if(mtlength > 0)
                    {
                        strstart = &nn_string_getdata(string)[mtstart];
                        rstr = nn_string_copylen(state, strstart, mtlength);
                        dm = nn_object_makedict(state);
                        nn_dict_addentrycstr(dm, "string", nn_value_fromobject(rstr));
                        nn_dict_addentrycstr(dm, "start", nn_value_makenumber(mtstart));
                        nn_dict_addentrycstr(dm, "length", nn_value_makenumber(mtlength));                        
                        nn_array_push(oa, nn_value_fromobject(dm));
                    }
                }
                return nn_value_fromobject(oa);
            }
            else
            {
                return nn_value_makebool(true);
            }
        }
    }
    else
    {
        nn_except_throwclass(state, state->exceptions.regexerror, pctx.errorbuf);
    }
    mrx_context_destroy(&pctx);
    if(capture)
    {
        return nn_value_makenull();
    }
    return nn_value_makebool(false);
}

NNValue nn_objfnstring_matchcapture(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNObjString* pattern;
    NNObjString* string;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "match", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isstring);
    string = nn_value_asstring(thisval);
    pattern = nn_value_asstring(argv[0]);
    return nn_util_stringregexmatch(state, string, pattern, true);
}

NNValue nn_objfnstring_matchonly(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNObjString* pattern;
    NNObjString* string;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "match", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isstring);
    string = nn_value_asstring(thisval);
    pattern = nn_value_asstring(argv[0]);
    return nn_util_stringregexmatch(state, string, pattern, false);
}

NNValue nn_objfnstring_count(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    int count;
    const char* tmp;
    NNObjString* substr;
    NNObjString* string;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "count", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isstring);
    string = nn_value_asstring(thisval);
    substr = nn_value_asstring(argv[0]);
    if(nn_string_getlength(substr) == 0 || nn_string_getlength(string) == 0)
    {
        return nn_value_makenumber(0);
    }
    count = 0;
    tmp = nn_string_getdata(string);
    while((tmp = nn_util_utf8strstr(tmp, nn_string_getdata(substr))))
    {
        count++;
        tmp++;
    }
    return nn_value_makenumber(count);
}

NNValue nn_objfnstring_tonumber(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNObjString* selfstr;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "toNumber", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    selfstr = nn_value_asstring(thisval);
    return nn_value_makenumber(strtod(nn_string_getdata(selfstr), NULL));
}

NNValue nn_objfnstring_isascii(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNObjString* string;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "isAscii", argv, argc);
    NEON_ARGS_CHECKCOUNTRANGE(&check, 0, 1);
    if(argc == 1)
    {
        NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isbool);
    }
    string = nn_value_asstring(thisval);
    return nn_value_fromobject(string);
}

NNValue nn_objfnstring_tolist(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    size_t i;
    size_t end;
    size_t start;
    size_t length;
    NNObjArray* list;
    NNObjString* string;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "toList", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    string = nn_value_asstring(thisval);
    list = (NNObjArray*)nn_gcmem_protect(state, (NNObject*)nn_object_makearray(state));
    length = nn_string_getlength(string);
    if(length > 0)
    {
        for(i = 0; i < length; i++)
        {
            start = i;
            end = i + 1;
            nn_array_push(list, nn_value_fromobject(nn_string_copylen(state, nn_string_getdata(string) + start, (int)(end - start))));
        }
    }
    return nn_value_fromobject(list);
}

NNValue nn_objfnstring_tobytes(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    size_t i;
    size_t length;
    NNObjArray* list;
    NNObjString* string;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "toBytes", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    string = nn_value_asstring(thisval);
    list = (NNObjArray*)nn_gcmem_protect(state, (NNObject*)nn_object_makearray(state));
    length = nn_string_getlength(string);
    if(length > 0)
    {
        for(i = 0; i < length; i++)
        {
            nn_array_push(list, nn_value_makenumber(nn_string_get(string, i)));
        }
    }
    return nn_value_fromobject(list);
}

NNValue nn_objfnstring_lpad(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    size_t i;
    size_t width;
    size_t fillsize;
    size_t finalsize;
    char fillchar;
    char* str;
    char* fill;
    NNObjString* ofillstr;
    NNObjString* result;
    NNObjString* string;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "lpad", argv, argc);
    NEON_ARGS_CHECKCOUNTRANGE(&check, 1, 2);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isnumber);
    string = nn_value_asstring(thisval);
    width = nn_value_asnumber(argv[0]);
    fillchar = ' ';
    if(argc == 2)
    {
        ofillstr = nn_value_asstring(argv[1]);
        fillchar = nn_string_get(ofillstr, 0);
    }
    if(width <= nn_string_getlength(string))
    {
        return thisval;
    }
    fillsize = width - nn_string_getlength(string);
    fill = (char*)nn_memory_malloc(sizeof(char) * ((size_t)fillsize + 1));
    finalsize = nn_string_getlength(string) + fillsize;
    for(i = 0; i < fillsize; i++)
    {
        fill[i] = fillchar;
    }
    str = (char*)nn_memory_malloc(sizeof(char) * ((size_t)finalsize + 1));
    memcpy(str, fill, fillsize);
    memcpy(str + fillsize, nn_string_getdata(string), nn_string_getlength(string));
    str[finalsize] = '\0';
    nn_memory_free(fill);
    result = nn_string_takelen(state, str, finalsize);
    nn_string_setlength(result, finalsize);
    return nn_value_fromobject(result);
}

NNValue nn_objfnstring_rpad(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    size_t i;
    size_t width;
    size_t fillsize;
    size_t finalsize;
    char fillchar;
    char* str;
    char* fill;
    NNObjString* ofillstr;
    NNObjString* string;
    NNObjString* result;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "rpad", argv, argc);
    NEON_ARGS_CHECKCOUNTRANGE(&check, 1, 2);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isnumber);
    string = nn_value_asstring(thisval);
    width = nn_value_asnumber(argv[0]);
    fillchar = ' ';
    if(argc == 2)
    {
        ofillstr = nn_value_asstring(argv[1]);
        fillchar = nn_string_get(ofillstr, 0);
    }
    if(width <= nn_string_getlength(string))
    {
        return thisval;
    }
    fillsize = width - nn_string_getlength(string);
    fill = (char*)nn_memory_malloc(sizeof(char) * ((size_t)fillsize + 1));
    finalsize = nn_string_getlength(string) + fillsize;
    for(i = 0; i < fillsize; i++)
    {
        fill[i] = fillchar;
    }
    str = (char*)nn_memory_malloc(sizeof(char) * ((size_t)finalsize + 1));
    memcpy(str, nn_string_getdata(string), nn_string_getlength(string));
    memcpy(str + nn_string_getlength(string), fill, fillsize);
    str[finalsize] = '\0';
    nn_memory_free(fill);
    result = nn_string_takelen(state, str, finalsize);
    nn_string_setlength(result, finalsize);
    return nn_value_fromobject(result);
}

NNValue nn_objfnstring_split(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    size_t i;
    size_t end;
    size_t start;
    size_t length;
    NNObjArray* list;
    NNObjString* string;
    NNObjString* delimeter;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "split", argv, argc);
    NEON_ARGS_CHECKCOUNTRANGE(&check, 1, 2);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isstring);
    string = nn_value_asstring(thisval);
    delimeter = nn_value_asstring(argv[0]);
    /* empty string matches empty string to empty list */
    if(((nn_string_getlength(string) == 0) && (nn_string_getlength(delimeter) == 0)) || (nn_string_getlength(string) == 0) || (nn_string_getlength(delimeter) == 0))
    {
        return nn_value_fromobject(nn_object_makearray(state));
    }
    list = (NNObjArray*)nn_gcmem_protect(state, (NNObject*)nn_object_makearray(state));
    if(nn_string_getlength(delimeter) > 0)
    {
        start = 0;
        for(i = 0; i <= nn_string_getlength(string); i++)
        {
            /* match found. */
            if(memcmp(nn_string_getdata(string) + i, nn_string_getdata(delimeter), nn_string_getlength(delimeter)) == 0 || i == nn_string_getlength(string))
            {
                nn_array_push(list, nn_value_fromobject(nn_string_copylen(state, nn_string_getdata(string) + start, i - start)));
                i += nn_string_getlength(delimeter) - 1;
                start = i + 1;
            }
        }
    }
    else
    {
        length = nn_string_getlength(string);
        for(i = 0; i < length; i++)
        {
            start = i;
            end = i + 1;
            nn_array_push(list, nn_value_fromobject(nn_string_copylen(state, nn_string_getdata(string) + start, (int)(end - start))));
        }
    }
    return nn_value_fromobject(list);
}

NNValue nn_objfnstring_replace(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    size_t i;
    size_t totallength;
    NNStringBuffer* result;
    NNObjString* substr;
    NNObjString* string;
    NNObjString* repsubstr;
    NNArgCheck check;
    (void)totallength;
    nn_argcheck_init(state, &check, "replace", argv, argc);
    NEON_ARGS_CHECKCOUNTRANGE(&check, 2, 3);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isstring);
    NEON_ARGS_CHECKTYPE(&check, 1, nn_value_isstring);
    string = nn_value_asstring(thisval);
    substr = nn_value_asstring(argv[0]);
    repsubstr = nn_value_asstring(argv[1]);
    if((nn_string_getlength(string) == 0 && nn_string_getlength(substr) == 0) || nn_string_getlength(string) == 0 || nn_string_getlength(substr) == 0)
    {
        return nn_value_fromobject(nn_string_copylen(state, nn_string_getdata(string), nn_string_getlength(string)));
    }
    result = nn_strbuf_makebasicempty(0);
    totallength = 0;
    for(i = 0; i < nn_string_getlength(string); i++)
    {
        if(memcmp(nn_string_getdata(string) + i, nn_string_getdata(substr), nn_string_getlength(substr)) == 0)
        {
            if(nn_string_getlength(substr) > 0)
            {
                nn_strbuf_appendstrn(result, nn_string_getdata(repsubstr), nn_string_getlength(repsubstr));
            }
            i += nn_string_getlength(substr) - 1;
            totallength += nn_string_getlength(repsubstr);
        }
        else
        {
            nn_strbuf_appendchar(result, nn_string_get(string, i));
            totallength++;
        }
    }
    return nn_value_fromobject(nn_string_makefromstrbuf(state, result, nn_util_hashstring(nn_strbuf_data(result), nn_strbuf_length(result))));
}

NNValue nn_objfnstring_iter(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    size_t index;
    size_t length;
    NNObjString* string;
    NNObjString* result;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "iter", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isnumber);
    string = nn_value_asstring(thisval);
    length = nn_string_getlength(string);
    index = nn_value_asnumber(argv[0]);
    if(((int)index > -1) && (index < length))
    {
        result = nn_string_copylen(state, &nn_string_getdata(string)[index], 1);
        return nn_value_fromobject(result);
    }
    return nn_value_makenull();
}

NNValue nn_objfnstring_itern(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    size_t index;
    size_t length;
    NNObjString* string;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "itern", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    string = nn_value_asstring(thisval);
    length = nn_string_getlength(string);
    if(nn_value_isnull(argv[0]))
    {
        if(length == 0)
        {
            return nn_value_makebool(false);
        }
        return nn_value_makenumber(0);
    }
    if(!nn_value_isnumber(argv[0]))
    {
        NEON_RETURNERROR("strings are numerically indexed");
    }
    index = nn_value_asnumber(argv[0]);
    if(index < length - 1)
    {
        return nn_value_makenumber((double)index + 1);
    }
    return nn_value_makenull();
}

NNValue nn_objfnstring_each(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    size_t i;
    size_t passi;
    size_t arity;
    NNValue callable;
    NNValue unused;
    NNObjString* string;
    NNArgCheck check;
    NNValue nestargs[3];
    nn_argcheck_init(state, &check, "each", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_iscallable);
    string = nn_value_asstring(thisval);
    callable = argv[0];
    arity = nn_nestcall_prepare(state, callable, thisval, nestargs, 2);
    for(i = 0; i < nn_string_getlength(string); i++)
    {
        passi = 0;
        if(arity > 0)
        {
            passi++;
            nestargs[0] = nn_value_fromobject(nn_string_copylen(state, nn_string_getdata(string) + i, 1));
            if(arity > 1)
            {
                passi++;
                nestargs[1] = nn_value_makenumber(i);
            }
        }
        nn_nestcall_callfunction(state, callable, thisval, nestargs, passi, &unused, false);
    }
    /* pop the argument list */
    return nn_value_makenull();
}

NNValue nn_objfnstring_appendany(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    size_t i;
    NNValue arg;
    NNObjString* oss;
    NNObjString* selfstring;
    selfstring = nn_value_asstring(thisval);
    for(i = 0; i < argc; i++)
    {
        arg = argv[i];
        if(nn_value_isnumber(arg))
        {
            nn_string_appendbyte(selfstring, nn_value_asnumber(arg));
        }
        else
        {
            oss = nn_value_tostring(state, arg);
            nn_string_appendobject(selfstring, oss);
        }
    }
    /* pop the argument list */
    return thisval;
}

NNValue nn_objfnstring_appendbytes(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    size_t i;
    NNValue arg;
    NNObjString* oss;
    NNObjString* selfstring;
    selfstring = nn_value_asstring(thisval);
    for(i = 0; i < argc; i++)
    {
        arg = argv[i];
        if(nn_value_isnumber(arg))
        {
            nn_string_appendbyte(selfstring, nn_value_asnumber(arg));
        }
        else
        {
            NEON_RETURNERROR("appendbytes expects number types");
        }
    }
    /* pop the argument list */
    return thisval;
}

void nn_state_installobjectstring(NNState* state)
{
    static NNConstClassMethodItem stringmethods[] =
    {
        {"@iter", nn_objfnstring_iter},
        {"@itern", nn_objfnstring_itern},
        {"size", nn_objfnstring_length},
        {"substr", nn_objfnstring_substring},
        {"substring", nn_objfnstring_substring},
        {"charCodeAt", nn_objfnstring_charcodeat},
        {"charAt", nn_objfnstring_charat},
        {"upper", nn_objfnstring_upper},
        {"lower", nn_objfnstring_lower},
        {"trim", nn_objfnstring_trim},
        {"ltrim", nn_objfnstring_ltrim},
        {"rtrim", nn_objfnstring_rtrim},
        {"split", nn_objfnstring_split},
        {"indexOf", nn_objfnstring_indexof},
        {"count", nn_objfnstring_count},
        {"toNumber", nn_objfnstring_tonumber},
        {"toList", nn_objfnstring_tolist},
        {"toBytes", nn_objfnstring_tobytes},
        {"lpad", nn_objfnstring_lpad},
        {"rpad", nn_objfnstring_rpad},
        {"replace", nn_objfnstring_replace},
        {"each", nn_objfnstring_each},
        {"startsWith", nn_objfnstring_startswith},
        {"endsWith", nn_objfnstring_endswith},
        {"isAscii", nn_objfnstring_isascii},
        {"isAlpha", nn_objfnstring_isalpha},
        {"isAlnum", nn_objfnstring_isalnum},
        {"isNumber", nn_objfnstring_isnumber},
        {"isFloat", nn_objfnstring_isfloat},
        {"isLower", nn_objfnstring_islower},
        {"isUpper", nn_objfnstring_isupper},
        {"isSpace", nn_objfnstring_isspace},
        {"utf8Chars", nn_objfnstring_utf8chars},
        {"utf8Codepoints", nn_objfnstring_utf8codepoints},
        {"utf8Bytes", nn_objfnstring_utf8codepoints},
        {"match", nn_objfnstring_matchcapture},
        {"matches", nn_objfnstring_matchonly},
        {"append", nn_objfnstring_appendany},
        {"push", nn_objfnstring_appendany},
        {"appendbytes", nn_objfnstring_appendbytes},
        {"appendbyte", nn_objfnstring_appendbytes},
        {NULL, NULL},
    };
    nn_class_defnativeconstructor(state->classprimstring, nn_objfnstring_constructor);
    nn_class_defstaticnativemethod(state->classprimstring, nn_string_copycstr(state, "fromCharCode"), nn_objfnstring_fromcharcode);
    nn_class_defstaticnativemethod(state->classprimstring, nn_string_copycstr(state, "utf8Decode"), nn_objfnstring_utf8decode);
    nn_class_defstaticnativemethod(state->classprimstring, nn_string_copycstr(state, "utf8Encode"), nn_objfnstring_utf8encode);
    nn_class_defstaticnativemethod(state->classprimstring, nn_string_copycstr(state, "utf8NumBytes"), nn_objfnstring_utf8numbytes);
    nn_class_defcallablefield(state->classprimstring, nn_string_copycstr(state, "length"), nn_objfnstring_length);
    nn_state_installmethods(state, state->classprimstring, stringmethods);

}

