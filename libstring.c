
#include "neon.h"
#include "mrx.h"

/*
* TODO: get rid of unused functions
*/

#if !defined(va_copy)
    #if defined(__GNUC__) || defined(__CLANG__)
        #define va_copy(d,s) __builtin_va_copy(d,s)
    #else
        #define va_copy(dest, src) memcpy(dest, src, sizeof(va_list))
    #endif
#endif

#if defined(__STRICT_ANSI__)
    void *memccpy(void *dest, const void *src, int c, size_t n);
    int vsnprintf(char *str, size_t size, const char *format, va_list ap);
#endif

#define STRBUF_MIN(x, y) ((x) < (y) ? (x) : (y))
#define STRBUF_MAX(x, y) ((x) > (y) ? (x) : (y))

#define nn_strbuf_exitonerror()     \
    do                      \
    {                       \
        abort();            \
        exit(EXIT_FAILURE); \
    } while(0)

/*
********************
*  Bounds checking
********************
*/

/* Bounds check when inserting (pos <= len are valid) */
#define nn_strbuf_boundscheckinsert(sbuf, pos) nn_strutil_callboundscheckinsert(sbuf, pos, __FILE__, __LINE__)
#define nn_strbuf_boundscheckreadrange(sbuf, start, len) nn_strutil_callboundscheckreadrange(sbuf, start, len, __FILE__, __LINE__)


#define ROUNDUP2POW(x) nn_strutil_rndup2pow64(x)


void nn_strformat_init(NNState* state, NNFormatInfo* nfi, NNPrinter* writer, const char* fmtstr, size_t fmtlen)
{
    nfi->pstate = state;
    nfi->fmtstr = fmtstr;
    nfi->fmtlen = fmtlen;
    nfi->writer = writer;
}

void nn_strformat_destroy(NNFormatInfo* nfi)
{
    (void)nfi;
}

bool nn_strformat_format(NNFormatInfo* nfi, int argc, int argbegin, NNValue* argv)
{
    int ch;
    int ival;
    int nextch;
    bool ok;
    size_t i;
    size_t argpos;
    NNValue cval;
    i = 0;
    argpos = argbegin;
    ok = true;
    while(i < nfi->fmtlen)
    {
        ch = nfi->fmtstr[i];
        nextch = -1;
        if((i + 1) < nfi->fmtlen)
        {
            nextch = nfi->fmtstr[i+1];
        }
        i++;
        if(ch == '%')
        {
            if(nextch == '%')
            {
                nn_printer_writechar(nfi->writer, '%');
            }
            else
            {
                i++;
                if((int)argpos > argc)
                {
                    nn_except_throwclass(nfi->pstate, nfi->pstate->exceptions.argumenterror, "too few arguments");
                    ok = false;
                    cval = nn_value_makenull();
                }
                else
                {
                    cval = argv[argpos];
                }
                argpos++;
                switch(nextch)
                {
                    case 'q':
                    case 'p':
                        {
                            nn_printer_printvalue(nfi->writer, cval, true, true);
                        }
                        break;
                    case 'c':
                        {
                            ival = (int)nn_value_asnumber(cval);
                            nn_printer_printf(nfi->writer, "%c", ival);
                        }
                        break;
                    /* TODO: implement actual field formatting */
                    case 's':
                    case 'd':
                    case 'i':
                    case 'g':
                        {
                            nn_printer_printvalue(nfi->writer, cval, false, true);
                        }
                        break;
                    default:
                        {
                            nn_except_throwclass(nfi->pstate, nfi->pstate->exceptions.argumenterror, "unknown/invalid format flag '%%c'", nextch);
                        }
                        break;
                }
            }
        }
        else
        {
            nn_printer_writechar(nfi->writer, ch);
        }
    }
    return ok;
}

size_t nn_strutil_rndup2pow64(uint64_t x)
{
    /* long long >=64 bits guaranteed in C99 */
    --x;
    x |= x >> 1;
    x |= x >> 2;
    x |= x >> 4;
    x |= x >> 8;
    x |= x >> 16;
    x |= x >> 32;
    ++x;
    return x;
}


/*
// Replaces `sep` with \0 in str
// Returns number of occurances of `sep` character in `str`
// Stores `nptrs` pointers in `ptrs`
*/
size_t nn_strutil_splitstr(char* str, char sep, char** ptrs, size_t nptrs)
{
    size_t n;
    n = 1;
    if(*str == '\0')
    {
        return 0;
    }
    if(nptrs > 0)
    {
        ptrs[0] = str;
    }
    while((str = strchr(str, sep)) != NULL)
    {
        *str = '\0';
        str++;
        if(n < nptrs)
        {
            ptrs[n] = str;
        }
        n++;
    }
    return n;
}

/*
// Replace one char with another in a string. Return number of replacements made
*/
size_t nn_strutil_charreplace(char* str, char from, char to)
{
    size_t n;
    n = 0;
    for(; *str; str++)
    {
        if(*str == from)
        {
            n++;
            *str = to;
        }
    }
    return n;
}

/*
// Reverse a string region
*/
void nn_strutil_reverseregion(char* str, size_t length)
{
    char *a;
    char* b;
    char tmp;
    a = str;
    b = str + length - 1;
    while(a < b)
    {
        tmp = *a;
        *a = *b;
        *b = tmp;
        a++;
        b--;
    }
}

bool nn_strutil_isallspace(const char* s)
{
    int i;
    for(i = 0; s[i] != '\0' && isspace((int)s[i]); i++)
    {
    }
    return (s[i] == '\0');
}

char* nn_strutil_nextspace(char* s)
{
    while(*s != '\0' && isspace((int)*s))
    {
        s++;
    }
    return (*s == '\0' ? NULL : s);
}

/*
// Strip whitespace the the start and end of a string.
// Strips whitepace from the end of the string with \0, and returns pointer to
// first non-whitespace character
*/
char* nn_strutil_trim(char* str)
{
    /* Work backwards */
    char* end;
    end = str + strlen(str);
    while(end > str && isspace((int)*(end - 1)))
    {
        end--;
    }
    *end = '\0';
    /* Work forwards: don't need start < len because will hit \0 */
    while(isspace((int)*str))
    {
        str++;
    }
    return str;
}

/*
// Removes \r and \n from the ends of a string and returns the new length
*/
size_t nn_strutil_chomp(char* str, size_t len)
{
    while(len > 0 && (str[len - 1] == '\r' || str[len - 1] == '\n'))
    {
        len--;
    }
    str[len] = '\0';
    return len;
}

/*
// Returns count
*/
size_t nn_strutil_countchar(const char* str, char c)
{
    size_t count;
    count = 0;
    while((str = strchr(str, c)) != NULL)
    {
        str++;
        count++;
    }
    return count;
}

/*
// Returns the number of strings resulting from the split
*/
size_t nn_strutil_split(const char* splitat, const char* sourcetxt, char*** result)
{
    size_t i;
    size_t slen;
    size_t count;
    size_t splitlen;
    size_t txtlen;
    char** arr;
    const char* find;
    const char* plastpos;
    splitlen = strlen(splitat);
    txtlen = strlen(sourcetxt);
    /* result is temporarily held here */
    if(splitlen == 0)
    {
        /* Special case */
        if(txtlen == 0)
        {
            *result = NULL;
            return 0;
        }
        else
        {
            arr = (char**)nn_memory_malloc(txtlen * sizeof(char*));
            for(i = 0; i < txtlen; i++)
            {
                arr[i] = (char*)nn_memory_malloc(2 * sizeof(char));
                arr[i][0] = sourcetxt[i];
                arr[i][1] = '\0';
            }
            *result = arr;
            return txtlen;
        }
    }
    find = sourcetxt;
    /* must have at least one item */
    count = 1;
    for(; (find = strstr(find, splitat)) != NULL; count++, find += splitlen)
    {
    }
    /* Create return array */
    arr = (char**)nn_memory_malloc(count * sizeof(char*));
    count = 0;
    plastpos = sourcetxt;
    while((find = strstr(plastpos, splitat)) != NULL)
    {
        slen = (size_t)(find - plastpos);
        arr[count] = (char*)nn_memory_malloc((slen + 1) * sizeof(char));
        strncpy(arr[count], plastpos, slen);
        arr[count][slen] = '\0';
        count++;
        plastpos = find + splitlen;
    }
    /* Copy last item */
    slen = (size_t)(sourcetxt + txtlen - plastpos);
    arr[count] = (char*)nn_memory_malloc((slen + 1) * sizeof(char));
    if(count == 0)
    {
        strcpy(arr[count], sourcetxt);
    }
    else
    {
        strncpy(arr[count], plastpos, slen);
    }
    arr[count][slen] = '\0';
    count++;
    *result = arr;
    return count;
}

void nn_strutil_callboundscheckinsert(const NNStringBuffer* sbuf, size_t pos, const char* file, int line)
{
    if(pos > sbuf->length)
    {
        fprintf(stderr, "%s:%i: - out of bounds error [index: %ld, num_of_bits: %ld]\n", file, line, (long)pos, (long)sbuf->length);
        errno = EDOM;
        nn_strbuf_exitonerror();
    }
}

/* Bounds check when reading a range (start+len < strlen is valid) */
void nn_strutil_callboundscheckreadrange(const NNStringBuffer* sbuf, size_t start, size_t len, const char* file, int line)
{
    if(start + len > sbuf->length)
    {
        fprintf(stderr,"%s:%i: - out of bounds error [start: %ld; length: %ld; strlen: %ld; buf:%.*s%s]\n",
                file, line, (long)start, (long)len, (long)sbuf->length, (int)STRBUF_MIN(5, sbuf->length), sbuf->data, sbuf->length > 5 ? "..." : "");
        errno = EDOM;
        nn_strbuf_exitonerror();
    }
}

/*
// Constructors / Destructors
*/


/*
// Place a string buffer into existing memory. Example:
//   NNStringBuffer buf;
//   nn_strbuf_makefromptr(&buf, 100);
//   ...
//   nn_strbuf_destroyfromptr(&buf);
*/
NNStringBuffer* nn_strbuf_makefromptr(NNStringBuffer* sbuf, size_t len)
{
    sbuf->isintern = false;
    sbuf->length = 0;
    #if 0
        sbuf->capacity = ROUNDUP2POW(len + 1);
    #else
        sbuf->capacity = len + 1;
    #endif
    sbuf->data = (char*)nn_memory_malloc(sbuf->capacity);
    if(!sbuf->data)
    {
        return NULL;
    }
    sbuf->data[0] = '\0';
    return sbuf;
}

bool nn_strbuf_initbasicempty(NNStringBuffer* sbuf, size_t len, bool onstack)
{
    sbuf->isintern = false;
    sbuf->length = len;
    sbuf->capacity = 0;
    sbuf->data = NULL;
    if(!nn_strbuf_makefromptr(sbuf, len))
    {
        if(!onstack)
        {
            nn_memory_free(sbuf);
        }
        return false;
    }
    return true;
}

bool nn_strbuf_makebasicemptystack(NNStringBuffer* sbuf, size_t len)
{
    return nn_strbuf_initbasicempty(sbuf, len, true);
}

NNStringBuffer* nn_strbuf_makebasicempty(size_t len)
{
    NNStringBuffer* sbuf;
    sbuf = (NNStringBuffer*)nn_memory_calloc(1, sizeof(NNStringBuffer));
    if(!sbuf)
    {
        return NULL;
    }
    if(!nn_strbuf_initbasicempty(sbuf, len, false))
    {
        return NULL;
    }
    return sbuf;
}

bool nn_strbuf_destroyfromstack(NNStringBuffer* sb)
{
    if(sb->isintern)
    {
        return true;
    }
    nn_memory_free(sb->data);
    return true;
}

bool nn_strbuf_destroy(NNStringBuffer* sb)
{
    if(!sb->isintern)
    {
        nn_memory_free(sb->data);
    }
    nn_memory_free(sb);
    return true;
}

bool nn_strbuf_destroyfromptr(NNStringBuffer* sb)
{
    if(!sb->isintern)
    {
        nn_memory_free(sb->data);
    }
    memset(sb, 0, sizeof(*sb));
    return true;
}

/*
// Copy a string or existing string buffer
*/
NNStringBuffer* nn_strbuf_makefromstring(const char* str, size_t slen)
{
    NNStringBuffer* sbuf;
    sbuf = nn_strbuf_makebasicempty(slen + 1);
    if(!sbuf)
    {
        return NULL;
    }
    sbuf->length = slen;
    memcpy(sbuf->data, str, slen);
    sbuf->data[sbuf->length] = '\0';
    return sbuf;
}

NNStringBuffer* nn_strbuf_makeclone(const NNStringBuffer* sbuf)
{
    /* One byte for the string end / null char \0 */
    NNStringBuffer* cpy;
    cpy = nn_strbuf_makebasicempty(sbuf->length + 1);
    if(!cpy)
    {
        return NULL;
    }
    cpy->length = sbuf->length;
    memcpy(cpy->data, sbuf->data, sbuf->length);
    cpy->data[cpy->length] = '\0';
    return cpy;
}


/* Clear the content of an existing NNStringBuffer (sets size to 0) */
void nn_strbuf_reset(NNStringBuffer* sb)
{
    if(sb->data)
    {
        memset(sb->data, 0, sb->length);
    }
    sb->length = 0;
}

/*
// Resizing
*/
void nn_strutil_cbufcapacity(char** buf, size_t* sizeptr, size_t len)
{
    /* for nul byte */
    len++;
    if(*sizeptr < len)
    {
        *sizeptr = ROUNDUP2POW(len);
        /* fprintf(stderr, "sizeptr=%ld\n", *sizeptr); */
        if((*buf = (char*)nn_memory_realloc(*buf, *sizeptr)) == NULL)
        {
            fprintf(stderr, "[%s:%i] Out of memory\n", __FILE__, __LINE__);
            abort();
        }
    }
}

/*
// Resize the buffer to have capacity to hold a string of length newlen
// (+ a null terminating character).  Can also be used to downsize the buffer's
// memory usage.  Returns 1 on success, 0 on failure.
*/
bool nn_strbuf_resize(NNStringBuffer* sbuf, size_t newlen)
{
    size_t capacity;
    char* newbuf;
    sbuf->isintern = false;
    capacity = ROUNDUP2POW(newlen + 1);
    newbuf = (char*)nn_memory_realloc(sbuf->data, capacity * sizeof(char));
    if(newbuf == NULL)
    {
        return false;
    }
    sbuf->data = newbuf;
    sbuf->capacity = capacity;
    if(sbuf->length > newlen)
    {
        /* Buffer was shrunk - re-add null byte */
        sbuf->length = newlen;
        sbuf->data[sbuf->length] = '\0';
    }
    return true;
}

/* Ensure capacity for len characters plus '\0' character - exits on FAILURE */
void nn_strbuf_ensurecapacity(NNStringBuffer* sb, size_t len)
{
    nn_strutil_cbufcapacity(&sb->data, &sb->capacity, len);
}

/* Same as above, but update pointer if it pointed to resized array */
void nn_strbuf_ensurecapacityupdateptr(NNStringBuffer* sbuf, size_t size, const char** ptr)
{
    size_t oldcap;
    char* oldbuf;
    if(sbuf->capacity <= size + 1)
    {
        oldcap = sbuf->capacity;
        oldbuf = sbuf->data;
        if(!nn_strbuf_resize(sbuf, size))
        {
            fprintf(stderr,
                    "%s:%i:Error: _ensure_capacity_update_ptr couldn't resize "
                    "buffer. [requested %ld bytes; capacity: %ld bytes]\n",
                    __FILE__, __LINE__, (long)size, (long)sbuf->capacity);
            nn_strbuf_exitonerror();
        }
        /* ptr may have pointed to sbuf, which has now moved */
        if(*ptr >= oldbuf && *ptr < oldbuf + oldcap)
        {
            *ptr = sbuf->data + (*ptr - oldbuf);
        }
    }
}

bool nn_strbuf_containschar(NNStringBuffer* sb, char ch)
{
    size_t i;
    for(i=0; i<sb->length; i++)
    {
        if(sb->data[i] == ch)
        {
            return true;
        }
    }
    return false;
}

/* via: https://codereview.stackexchange.com/q/274832 */
void nn_strutil_faststrncat(char *dest, const char *src, size_t *size)
{
    if(dest && src && size)
    {
        while((dest[*size] = *src++))
        {
            *size += 1;
        }
    }
}

size_t nn_strutil_strreplace1(char **str, size_t selflen, const char* findstr, size_t findlen, const char *substr, size_t sublen)
{
    size_t i;
    size_t x;
    size_t oldcount;
    char* buff;
    const char *temp;
    (void)selflen;
    oldcount = 0;
    temp = (const char *)(*str);
    for (i = 0; temp[i] != '\0'; ++i)
    {
        if (strstr((const char *)&temp[i], findstr) == &temp[i])
        {
            oldcount++;
            i += findlen - 1;
        }
    }
    buff = (char*)nn_memory_calloc((i + oldcount * (sublen - findlen) + 1), sizeof(char));
    if (!buff)
    {
        perror("bad allocation\n");
        exit(EXIT_FAILURE);
    }
    i = 0;
    while (*temp)
    {
        if (strstr(temp, findstr) == temp)
        {
            x = 0;
            nn_strutil_faststrncat(&buff[i], substr, &x);
            i += sublen;
            temp += findlen;
        }
        else
        {
            buff[i++] = *temp++;
        }
    }
    nn_memory_free(*str);
    *str = (char*)nn_memory_calloc(i + 1, sizeof(char));
    if (!(*str))
    {
        perror("bad allocation\n");
        exit(EXIT_FAILURE);
    }
    i = 0;
    nn_strutil_faststrncat(*str, (const char *)buff, &i);
    nn_memory_free(buff);
    return i;
}

size_t nn_strutil_strrepcount(const char* str, size_t slen, const char* findstr, size_t findlen, size_t sublen)
{
    size_t i;
    size_t count;
    size_t total;
    (void)total;
    total = slen;
    count = 0;
    for(i=0; i<slen; i++)
    {
        if(str[i] == findstr[0])
        {
            if((i + findlen) < slen)
            {
                if(memcmp(&str[i], findstr, findlen) == 0)
                {
                    count++;
                    total += sublen;
                }
            }
        }
    }
    if(count == 0)
    {
        return 0;
    }
    return total + 0;
}

/* via: https://stackoverflow.com/a/32413923 */
void nn_strutil_strreplace2(char* target, size_t tgtlen, const char *findstr, size_t findlen, const char *substr, size_t sublen)
{
    const char *p;
    const char *tmp;
    char *inspoint;
    char buffer[1024] = {0};
    (void)tgtlen;
    inspoint = &buffer[0];
    tmp = target;
    while(true)
    {
        p = strstr(tmp, findstr);
        /* walked past last occurrence of findstr; copy remaining part */
        if (p == NULL)
        {
            strcpy(inspoint, tmp);
            break;
        }
        /* copy part before findstr */
        memcpy(inspoint, tmp, p - tmp);
        inspoint += p - tmp;
        /* copy substr string */
        memcpy(inspoint, substr, sublen);
        inspoint += sublen;
        /* adjust pointers, move on */
        tmp = p + findlen;
    }
    /* write altered string back to target */
    strcpy(target, buffer);
}

bool nn_strbuf_fullreplace(NNStringBuffer* sb, const char* findstr, size_t findlen, const char* substr, size_t sublen)
{
    size_t nl;
    size_t needed;
    needed = nn_strutil_strrepcount(sb->data, sb->length, findstr, findlen, sublen);
    if(needed == 0)
    {
        return false;
    }
    nn_strbuf_resize(sb, sb->capacity + needed);
    nl = nn_strutil_strreplace1(&sb->data, sb->length, findstr, findlen, substr, sublen);
    sb->length = nl;
    return true;
}

bool nn_strutil_inpreplhelper(char *dest, const char *src, size_t srclen, int findme, const char* substr, size_t sublen, size_t maxlen, size_t* dlen)
{
    /* ch(ar) at pos(ition) */
    int chatpos;
    /* printf("'%p' '%s' %c\n", dest, src, findme); */
    if(*src == findme)
    {
        if(sublen > maxlen)
        {
            return false;
        }
        if(!nn_strutil_inpreplhelper(dest + sublen, src + 1, srclen, findme, substr, sublen, maxlen - sublen, dlen))
        {
            return false;
        }
        memcpy(dest, substr, sublen);
        *dlen += sublen;
        return true;
    }
    if(maxlen == 0)
    {
        return false;
    }
    chatpos = *src;
    if(*src)
    {
        *dlen += 1;
        if(!nn_strutil_inpreplhelper(dest + 1, src + 1, srclen, findme, substr, sublen, maxlen - 1, dlen))
        {
            return false;
        }
    }
    *dest = chatpos;
    return true;
}

size_t nn_strutil_inpreplace(char* target, size_t tgtlen, int findme, const char* substr, size_t sublen, size_t maxlen)
{
    size_t nlen;
    if(findme == 0)
    {
        return 0;
    }
    if(maxlen == 0)
    {
        return 0;
    }
    if(*substr == 0)
    {
        /* Insure target does not shrink. */
        return 0;
    }
    nlen = 0;
    nn_strutil_inpreplhelper(target, target, tgtlen, findme, substr, sublen, maxlen - 1, &nlen);
    return nlen;
}

bool nn_strbuf_charreplace(NNStringBuffer* sb, int findme, const char* substr, size_t sublen)
{
    size_t i;
    size_t nlen;
    size_t needed;
    needed = sb->capacity;
    for(i=0; i<sb->length; i++)
    {
        if(sb->data[i] == findme)
        {
            needed += sublen;
        }
    }
    if(!nn_strbuf_resize(sb, needed+1))
    {
        return false;
    }
    nlen = nn_strutil_inpreplace(sb->data, sb->length, findme, substr, sublen, sb->capacity);
    sb->length = nlen;
    return true;
}

/* Set string buffer to contain a given string */
void nn_strbuf_set(NNStringBuffer* sb, const char* str)
{
    size_t len;
    len = strlen(str);
    nn_strbuf_ensurecapacity(sb, len);
    memcpy(sb->data, str, len);
    sb->data[sb->length = len] = '\0';
}

/* Set string buffer to match existing string buffer */
void nn_strbuf_setbuff(NNStringBuffer* dest, NNStringBuffer* from)
{
    nn_strbuf_ensurecapacity(dest, from->length);
    memmove(dest->data, from->data, from->length);
    dest->data[dest->length = from->length] = '\0';
}

/* Add a character to the end of this NNStringBuffer */
bool nn_strbuf_appendchar(NNStringBuffer* sb, int c)
{
    nn_strbuf_ensurecapacity(sb, sb->length + 1);
    sb->data[sb->length] = c;
    sb->data[++sb->length] = '\0';
    return true;
}

/*
// Copy N characters from a character array to the end of this NNStringBuffer
// strlen(str) must be >= len
*/
bool nn_strbuf_appendstrn(NNStringBuffer* sb, const char* str, size_t len)
{
    if(len > 0)
    {
        nn_strbuf_ensurecapacityupdateptr(sb, sb->length + len, &str);
        memcpy(sb->data + sb->length, str, len);
        sb->data[sb->length = sb->length + len] = '\0';
    }
    return true;
}

/* Copy a character array to the end of this NNStringBuffer */
bool nn_strbuf_appendstr(NNStringBuffer* sb, const char* str)
{
    return nn_strbuf_appendstrn(sb, str, strlen(str));
}

bool nn_strbuf_appendbuff(NNStringBuffer* sb1, const NNStringBuffer* sb2)
{
    return nn_strbuf_appendstrn(sb1, sb2->data, sb2->length);
}


/*
 * Integer to string functions adapted from:
 *   https://www.facebook.com/notes/facebook-engineering/three-optimization-tips-for-c/10151361643253920
 */

#define DYN_STRCONST_P01 10
#define DYN_STRCONST_P02 100
#define DYN_STRCONST_P03 1000
#define DYN_STRCONST_P04 10000
#define DYN_STRCONST_P05 100000
#define DYN_STRCONST_P06 1000000
#define DYN_STRCONST_P07 10000000
#define DYN_STRCONST_P08 100000000
#define DYN_STRCONST_P09 1000000000
#define DYN_STRCONST_P10 10000000000
#define DYN_STRCONST_P11 100000000000
#define DYN_STRCONST_P12 1000000000000

/**
 * Return number of digits required to represent `num` in base 10.
 * Uses binary search to find number.
 * Examples:
 *   nn_strutil_numofdigits(0)   = 1
 *   nn_strutil_numofdigits(1)   = 1
 *   nn_strutil_numofdigits(10)  = 2
 *   nn_strutil_numofdigits(123) = 3
 */
size_t nn_strutil_numofdigits(unsigned long v)
{
    if(v < DYN_STRCONST_P01)
    {
        return 1;
    }
    if(v < DYN_STRCONST_P02)
    {
        return 2;
    }
    if(v < DYN_STRCONST_P03)
    {
        return 3;
    }
    if(v < DYN_STRCONST_P12)
    {
        if(v < DYN_STRCONST_P08)
        {
            if(v < DYN_STRCONST_P06)
            {
                if(v < DYN_STRCONST_P04)
                {
                    return 4;
                }
                return 5 + (v >= DYN_STRCONST_P05);
            }
            return 7 + (v >= DYN_STRCONST_P07);
        }
        if(v < DYN_STRCONST_P10)
        {
            return 9 + (v >= DYN_STRCONST_P09);
        }
        return 11 + (v >= DYN_STRCONST_P11);
    }
    return 12 + nn_strutil_numofdigits(v / DYN_STRCONST_P12);
}


/* Convert integers to string to append */
bool nn_strbuf_appendnumulong(NNStringBuffer* buf, unsigned long value)
{
    size_t v;
    size_t pos;
    size_t numdigits;
    char* dst;
    /* Append two digits at a time */
    static const char* digits = (
        "0001020304050607080910111213141516171819"
        "2021222324252627282930313233343536373839"
        "4041424344454647484950515253545556575859"
        "6061626364656667686970717273747576777879"
        "8081828384858687888990919293949596979899"
    );
    numdigits = nn_strutil_numofdigits(value);
    pos = numdigits - 1;
    nn_strbuf_ensurecapacity(buf, buf->length + numdigits);
    dst = buf->data + buf->length;
    while(value >= 100)
    {
        v = value % 100;
        value /= 100;
        dst[pos] = digits[v * 2 + 1];
        dst[pos - 1] = digits[v * 2];
        pos -= 2;
    }
    /* Handle last 1-2 digits */
    if(value < 10)
    {
        dst[pos] = '0' + value;
    }
    else
    {
        dst[pos] = digits[value * 2 + 1];
        dst[pos - 1] = digits[value * 2];
    }
    buf->length += numdigits;
    buf->data[buf->length] = '\0';
    return true;
}

bool nn_strbuf_appendnumlong(NNStringBuffer* buf, long value)
{
    /* nn_strbuf_appendformat(buf, "%li", value); */
    if(value < 0)
    {
        nn_strbuf_appendchar(buf, '-');
        value = -value;
    }
    return nn_strbuf_appendnumulong(buf, value);
}


bool nn_strbuf_appendnumint(NNStringBuffer* buf, int value)
{
    /* nn_strbuf_appendformat(buf, "%i", value); */
    return nn_strbuf_appendnumlong(buf, value);
}


/* Append string converted to lowercase */
bool nn_strbuf_appendstrnlowercase(NNStringBuffer* buf, const char* str, size_t len)
{
    char* to;
    const char* plength;
    nn_strbuf_ensurecapacity(buf, buf->length + len);
    to = buf->data + buf->length;
    plength = str + len;
    for(; str < plength; str++, to++)
    {
        *to = tolower(*str);
    }
    buf->length += len;
    buf->data[buf->length] = '\0';
    return true;
}

/* Append string converted to uppercase */
bool nn_strbuf_appendstrnuppercase(NNStringBuffer* buf, const char* str, size_t len)
{
    char* to;
    const char* end;
    nn_strbuf_ensurecapacity(buf, buf->length + len);
    to = buf->data + buf->length;
    end = str + len;
    for(; str < end; str++, to++)
    {
        *to = toupper(*str);
    }
    buf->length += len;
    buf->data[buf->length] = '\0';
    return true;
}


/* Append char `c` `n` times */
bool nn_strbuf_appendcharn(NNStringBuffer* buf, char c, size_t n)
{
    nn_strbuf_ensurecapacity(buf, buf->length + n);
    memset(buf->data + buf->length, c, n);
    buf->length += n;
    buf->data[buf->length] = '\0';
    return true;
}

void nn_strbuf_shrink(NNStringBuffer* sb, size_t len)
{
    sb->data[sb->length = (len)] = 0;
}

/*
// Remove \r and \n characters from the end of this StringBuffesr
// Returns the number of characters removed
*/
size_t nn_strbuf_chomp(NNStringBuffer* sbuf)
{
    size_t oldlen;
    oldlen = sbuf->length;
    sbuf->length = nn_strutil_chomp(sbuf->data, sbuf->length);
    return oldlen - sbuf->length;
}

/* Reverse a string */
void nn_strbuf_reverse(NNStringBuffer* sbuf)
{
    nn_strutil_reverseregion(sbuf->data, sbuf->length);
}

/*
// Get a substring as a new null terminated char array
// (remember to free the returned char* after you're done with it!)
*/
char* nn_strbuf_substr(const NNStringBuffer* sbuf, size_t start, size_t len)
{
    char* newstr;
    nn_strbuf_boundscheckreadrange(sbuf, start, len);
    newstr = (char*)nn_memory_malloc((len + 1) * sizeof(char));
    strncpy(newstr, sbuf->data + start, len);
    newstr[len] = '\0';
    return newstr;
}

void nn_strbuf_touppercase(NNStringBuffer* sbuf)
{
    char* pos;
    char* end;
    end = sbuf->data + sbuf->length;
    for(pos = sbuf->data; pos < end; pos++)
    {
        *pos = (char)toupper(*pos);
    }
}

void nn_strbuf_tolowercase(NNStringBuffer* sbuf)
{
    char* pos;
    char* end;
    end = sbuf->data + sbuf->length;
    for(pos = sbuf->data; pos < end; pos++)
    {
        *pos = (char)tolower(*pos);
    }
}

/*
// Copy a string to this NNStringBuffer, overwriting any existing characters
// Note: dstpos + len can be longer the the current dst NNStringBuffer
*/
void nn_strbuf_copyover(NNStringBuffer* dst, size_t dstpos, const char* src, size_t len)
{
    size_t newlen;
    if(src == NULL || len == 0)
    {
        return;
    }
    nn_strbuf_boundscheckinsert(dst, dstpos);
    /*
    // Check if dst buffer can handle string
    // src may have pointed to dst, which has now moved
    */
    newlen = STRBUF_MAX(dstpos + len, dst->length);
    nn_strbuf_ensurecapacityupdateptr(dst, newlen, &src);
    /* memmove instead of strncpy, as it can handle overlapping regions */
    memmove(dst->data + dstpos, src, len * sizeof(char));
    if(dstpos + len > dst->length)
    {
        /* Extended string - add '\0' char */
        dst->length = dstpos + len;
        dst->data[dst->length] = '\0';
    }
}

/* Insert: copy to a NNStringBuffer, shifting any existing characters along */
void nn_strbuf_insert(NNStringBuffer* dst, size_t dstpos, const char* src, size_t len)
{
    char* insert;
    if(src == NULL || len == 0)
    {
        return;
    }
    nn_strbuf_boundscheckinsert(dst, dstpos);
    /*
    // Check if dst buffer has capacity for inserted string plus \0
    // src may have pointed to dst, which will be moved in realloc when
    // calling ensure capacity
    */
    nn_strbuf_ensurecapacityupdateptr(dst, dst->length + len, &src);
    insert = dst->data + dstpos;
    /* dstpos could be at the end (== dst->length) */
    if(dstpos < dst->length)
    {
        /* Shift some characters up */
        memmove(insert + len, insert, (dst->length - dstpos) * sizeof(char));
        if(src >= dst->data && src < dst->data + dst->capacity)
        {
            /* src/dst strings point to the same string in memory */
            if(src < insert)
            {
                memmove(insert, src, len * sizeof(char));
            }
            else if(src > insert)
            {
                memmove(insert, src + len, len * sizeof(char));
            }
        }
        else
        {
            memmove(insert, src, len * sizeof(char));
        }
    }
    else
    {
        memmove(insert, src, len * sizeof(char));
    }
    /* Update size */
    dst->length += len;
    dst->data[dst->length] = '\0';
}

/*
// Overwrite dstpos..(dstpos+dstlen-1) with srclen chars from src
// if dstlen != srclen, content to the right of dstlen is shifted
// Example:
//   nn_strbuf_set(sbuf, "aaabbccc");
//   char *data = "xxx";
//   nn_strbuf_overwrite(sbuf,3,2,data,strlen(data));
//   // sbuf is now "aaaxxxccc"
//   nn_strbuf_overwrite(sbuf,3,2,"_",1);
//   // sbuf is now "aaa_ccc"
*/
void nn_strbuf_overwrite(NNStringBuffer* dst, size_t dstpos, size_t dstlen, const char* src, size_t srclen)
{
    size_t len;
    size_t newlen;
    char* tgt;
    char* end;
    nn_strbuf_boundscheckreadrange(dst, dstpos, dstlen);
    if(src == NULL)
    {
        return;
    }
    if(dstlen == srclen)
    {
        nn_strbuf_copyover(dst, dstpos, src, srclen);
    }
    newlen = dst->length + srclen - dstlen;
    nn_strbuf_ensurecapacityupdateptr(dst, newlen, &src);
    if(src >= dst->data && src < dst->data + dst->capacity)
    {
        if(srclen < dstlen)
        {
            /* copy */
            memmove(dst->data + dstpos, src, srclen * sizeof(char));
            /* resize (shrink) */
            memmove(dst->data + dstpos + srclen, dst->data + dstpos + dstlen, (dst->length - dstpos - dstlen) * sizeof(char));
        }
        else
        {
            /*
            // Buffer is going to grow and src points to this buffer
            // resize (grow)
            */
            memmove(dst->data + dstpos + srclen, dst->data + dstpos + dstlen, (dst->length - dstpos - dstlen) * sizeof(char));
            tgt = dst->data + dstpos;
            end = dst->data + dstpos + srclen;
            if(src < tgt + dstlen)
            {
                len = STRBUF_MIN((size_t)(end - src), srclen);
                memmove(tgt, src, len);
                tgt += len;
                src += len;
                srclen -= len;
            }
            if(src >= tgt + dstlen)
            {
                /* shift to account for resizing */
                src += srclen - dstlen;
                memmove(tgt, src, srclen);
            }
        }
    }
    else
    {
        /* resize */
        memmove(dst->data + dstpos + srclen, dst->data + dstpos + dstlen, (dst->length - dstpos - dstlen) * sizeof(char));
        /* copy */
        memcpy(dst->data + dstpos, src, srclen * sizeof(char));
    }
    dst->length = newlen;
    dst->data[dst->length] = '\0';
}

/*
// Remove characters from the buffer
//   nn_strbuf_set(sb, "aaaBBccc");
//   nn_strbuf_erase(sb, 3, 2);
//   // sb is now "aaaccc"
*/
void nn_strbuf_erase(NNStringBuffer* sbuf, size_t pos, size_t len)
{
    nn_strbuf_boundscheckreadrange(sbuf, pos, len);
    memmove(sbuf->data + pos, sbuf->data + pos + len, sbuf->length - pos - len);
    sbuf->length -= len;
    sbuf->data[sbuf->length] = '\0';
}

/*
// sprintf
*/

int nn_strbuf_appendformatposv(NNStringBuffer* sbuf, size_t pos, const char* fmt, va_list argptr)
{
    size_t buflen;
    int numchars;
    va_list vacpy;
    nn_strbuf_boundscheckinsert(sbuf, pos);
    /* Length of remaining buffer */
    buflen = sbuf->capacity - pos;
    if(buflen == 0 && !nn_strbuf_resize(sbuf, sbuf->capacity << 1))
    {
        fprintf(stderr, "%s:%i:Error: Out of memory\n", __FILE__, __LINE__);
        nn_strbuf_exitonerror();
    }
    /* Make a copy of the list of args incase we need to resize buff and try again */
    va_copy(vacpy, argptr);
    numchars = vsnprintf(sbuf->data + pos, buflen, fmt, argptr);
    va_end(argptr);
    /*
    // numchars is the number of chars that would be written (not including '\0')
    // numchars < 0 => failure
    */
    if(numchars < 0)
    {
        fprintf(stderr, "Warning: nn_strbuf_appendformatv something went wrong..\n");
        nn_strbuf_exitonerror();
    }
    /* numchars does not include the null terminating byte */
    if((size_t)numchars + 1 > buflen)
    {
        nn_strbuf_ensurecapacity(sbuf, pos + (size_t)numchars);
        /*
        // now use the argptr copy we made earlier
        // Don't need to use vsnprintf now, vsprintf will do since we know it'll fit
        */
        numchars = vsprintf(sbuf->data + pos, fmt, vacpy);
        if(numchars < 0)
        {
            fprintf(stderr, "Warning: nn_strbuf_appendformatv something went wrong..\n");
            nn_strbuf_exitonerror();
        }
    }
    va_end(vacpy);
    /*
    // Don't need to NUL terminate, vsprintf/vnsprintf does that for us
    // Update length
    */
    sbuf->length = pos + (size_t)numchars;
    return numchars;
}

int nn_strbuf_appendformatv(NNStringBuffer* sbuf, const char* fmt, va_list argptr)
{
    return nn_strbuf_appendformatposv(sbuf, sbuf->length, fmt, argptr);
}

/* sprintf to the end of a NNStringBuffer (adds string terminator after sprint) */
int nn_strbuf_appendformat(NNStringBuffer* sbuf, const char* fmt, ...)
{
    int numchars;
    va_list argptr;
    va_start(argptr, fmt);
    numchars = nn_strbuf_appendformatposv(sbuf, sbuf->length, fmt, argptr);
    va_end(argptr);
    return numchars;
}

/* Print at a given position (overwrite chars at positions >= pos) */
int nn_strbuf_appendformatat(NNStringBuffer* sbuf, size_t pos, const char* fmt, ...)
{
    int numchars;
    va_list argptr;
    nn_strbuf_boundscheckinsert(sbuf, pos);
    va_start(argptr, fmt);
    numchars = nn_strbuf_appendformatposv(sbuf, pos, fmt, argptr);
    va_end(argptr);
    return numchars;
}

/*
// sprintf without terminating character
// Does not prematurely end the string if you sprintf within the string
// (terminates string if sprintf to the end)
// Does not prematurely end the string if you sprintf within the string
// (vs at the end)
*/
int nn_strbuf_appendformatnoterm(NNStringBuffer* sbuf, size_t pos, const char* fmt, ...)
{
    size_t len;
    int nchars;
    char lastchar;
    va_list argptr;
    nn_strbuf_boundscheckinsert(sbuf, pos);
    len = sbuf->length;
    /* Call vsnprintf with NULL, 0 to get resulting string length without writing */
    va_start(argptr, fmt);
    nchars = vsnprintf(NULL, 0, fmt, argptr);
    va_end(argptr);
    if(nchars < 0)
    {
        fprintf(stderr, "Warning: nn_strbuf_appendformatv something went wrong..\n");
        nn_strbuf_exitonerror();
    }
    /* Save overwritten char */
    lastchar = (pos + (size_t)nchars < sbuf->length) ? sbuf->data[pos + (size_t)nchars] : 0;
    va_start(argptr, fmt);
    nchars = nn_strbuf_appendformatposv(sbuf, pos, fmt, argptr);
    va_end(argptr);
    if(nchars < 0)
    {
        fprintf(stderr, "Warning: nn_strbuf_appendformatv something went wrong..\n");
        nn_strbuf_exitonerror();
    }
    /* Restore length if shrunk, null terminate if extended */
    if(sbuf->length < len)
    {
        sbuf->length = len;
    }
    else
    {
        sbuf->data[sbuf->length] = '\0';
    }
    /* Re-instate overwritten character */
    sbuf->data[pos + (size_t)nchars] = lastchar;
    return nchars;
}

/* Trim whitespace characters from the start and end of a string */
void nn_strbuf_triminplace(NNStringBuffer* sbuf)
{
    size_t start;
    if(sbuf->length == 0)
    {
        return;
    }
    /* Trim end first */
    while(sbuf->length > 0 && isspace((int)sbuf->data[sbuf->length - 1]))
    {
        sbuf->length--;
    }
    sbuf->data[sbuf->length] = '\0';
    if(sbuf->length == 0)
    {
        return;
    }
    start = 0;
    while(start < sbuf->length && isspace((int)sbuf->data[start]))
    {
        start++;
    }
    if(start != 0)
    {
        sbuf->length -= start;
        memmove(sbuf->data, sbuf->data + start, sbuf->length * sizeof(char));
        sbuf->data[sbuf->length] = '\0';
    }
}

/*
// Trim the characters listed in `list` from the left of `sbuf`
// `list` is a null-terminated string of characters
*/
void nn_strbuf_trimleftinplace(NNStringBuffer* sbuf, const char* list)
{
    size_t start;
    start = 0;

    while(start < sbuf->length && (strchr(list, sbuf->data[start]) != NULL))
    {
        start++;
    }
    if(start != 0)
    {
        sbuf->length -= start;
        memmove(sbuf->data, sbuf->data + start, sbuf->length * sizeof(char));
        sbuf->data[sbuf->length] = '\0';
    }
}

/*
// Trim the characters listed in `list` from the right of `sbuf`
// `list` is a null-terminated string of characters
*/
void nn_strbuf_trimrightinplace(NNStringBuffer* sbuf, const char* list)
{
    if(sbuf->length == 0)
    {
        return;
    }
    while(sbuf->length > 0 && strchr(list, sbuf->data[sbuf->length - 1]) != NULL)
    {
        sbuf->length--;
    }
    sbuf->data[sbuf->length] = '\0';
}



NNObjString* nn_string_makefromstrbuf(NNState* state, NNStringBuffer* sbuf, uint32_t hsv)
{
    NNObjString* rs;
    rs = (NNObjString*)nn_object_allocobject(state, sizeof(NNObjString), NEON_OBJTYPE_STRING, false);
    rs->sbuf = *sbuf;
    rs->hashvalue = hsv;
    nn_vm_stackpush(state, nn_value_fromobject(rs));
    nn_valtable_set(&state->allocatedstrings, nn_value_fromobject(rs), nn_value_makenull());
    nn_vm_stackpop(state);
    return rs;
}

size_t nn_string_getlength(NNObjString* os)
{
    return os->sbuf.length;
}

const char* nn_string_getdata(NNObjString* os)
{
    return os->sbuf.data;
}

const char* nn_string_getcstr(NNObjString* os)
{
    return nn_string_getdata(os);
}

void nn_string_destroy(NNState* state, NNObjString* str)
{
    nn_strbuf_destroyfromstack(&str->sbuf);
    nn_gcmem_release(state, str, sizeof(NNObjString));
}

NNObjString* nn_string_internlen(NNState* state, const char* chars, int length)
{
    uint32_t hsv;
    NNStringBuffer sbuf;
    hsv = nn_util_hashstring(chars, length);
    memset(&sbuf, 0, sizeof(NNStringBuffer));
    sbuf.data = (char*)chars;
    sbuf.length = length;
    sbuf.isintern = true;
    return nn_string_makefromstrbuf(state, &sbuf, hsv);
}

NNObjString* nn_string_intern(NNState* state, const char* chars)
{
    return nn_string_internlen(state, chars, strlen(chars));
}

NNObjString* nn_string_takelen(NNState* state, char* chars, int length)
{
    uint32_t hsv;
    NNObjString* rs;
    NNStringBuffer sbuf;
    hsv = nn_util_hashstring(chars, length);
    rs = nn_valtable_findstring(&state->allocatedstrings, chars, length, hsv);
    if(rs != NULL)
    {
        nn_memory_free(chars);
        return rs;
    }
    memset(&sbuf, 0, sizeof(NNStringBuffer));
    sbuf.data = chars;
    sbuf.length = length;
    return nn_string_makefromstrbuf(state, &sbuf, hsv);
}

NNObjString* nn_string_takecstr(NNState* state, char* chars)
{
    return nn_string_takelen(state, chars, strlen(chars));
}

NNObjString* nn_string_copylen(NNState* state, const char* chars, int length)
{
    uint32_t hsv;
    NNStringBuffer sbuf;
    NNObjString* rs;
    hsv = nn_util_hashstring(chars, length);
    rs = nn_valtable_findstring(&state->allocatedstrings, chars, length, hsv);
    if(rs != NULL)
    {
        return rs;
    }
    memset(&sbuf, 0, sizeof(NNStringBuffer));
    nn_strbuf_makebasicemptystack(&sbuf, length);
    nn_strbuf_appendstrn(&sbuf, chars, length);
    rs = nn_string_makefromstrbuf(state, &sbuf, hsv);
    return rs;
}

NNObjString* nn_string_copycstr(NNState* state, const char* chars)
{
    return nn_string_copylen(state, chars, strlen(chars));
}

NNObjString* nn_string_copyobject(NNState* state, NNObjString* origos)
{
    return nn_string_copylen(state, origos->sbuf.data, origos->sbuf.length);
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
    return nn_string_appendstringlen(os, other->sbuf.data, other->sbuf.length);
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
    return nn_string_substrlen(os, start, os->sbuf.length);
}

size_t nn_string_chompinplace(NNObjString* os)
{
    return nn_strbuf_chomp(&os->sbuf);
}

NNObjString* nn_string_chomp(NNObjString* os)
{
    NNObjString* r;
    r = nn_string_copyobject(((NNObject*)os)->pstate, os);
    nn_string_chompinplace(r);
    return r;
}

void nn_string_reverseinplace(NNObjString* os)
{
    return nn_strbuf_reverse(&os->sbuf);
}

NNObjString* nn_string_reverse(NNObjString* os)
{
    NNObjString* r;
    r = nn_string_copyobject(((NNObject*)os)->pstate, os);
    nn_string_reverseinplace(r);
    return r;
}

void nn_string_triminplace(NNObjString* os)
{
    nn_strbuf_triminplace(&os->sbuf);
}

NNObjString* nn_string_trim(NNObjString* os)
{
    NNObjString* r;
    r = nn_string_copyobject(((NNObject*)os)->pstate, os);
    nn_string_triminplace(r);
    return r;
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
    res = nn_util_utf8decode((const uint8_t*)instr->sbuf.data, instr->sbuf.length);
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
    nn_utf8iter_init(&iter, instr->sbuf.data, instr->sbuf.length);
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
    return nn_value_makenumber(selfstr->sbuf.length);
}

NNValue nn_string_fromrange(NNState* state, const char* buf, int len)
{
    NNObjString* str;
    if(len <= 0)
    {
        return nn_value_fromobject(nn_string_copylen(state, "", 0));
    }
    str = nn_string_copylen(state, "", 0);
    nn_strbuf_appendstrn(&str->sbuf, buf, len);
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
    maxlen = selfstr->sbuf.length;
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
    memcpy(raw, selfstr->sbuf.data + start, len);
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
    maxlen = selfstr->sbuf.length;
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
    selflen = (int)selfstr->sbuf.length;
    if((idx < 0) || (idx >= selflen))
    {
        ch = -1;
    }
    else
    {
        ch = selfstr->sbuf.data[idx];
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
    selflen = (int)selfstr->sbuf.length;
    if((idx < 0) || (idx >= selflen))
    {
        return nn_value_fromobject(nn_string_copylen(state, "", 0));
    }
    else
    {
        ch = selfstr->sbuf.data[idx];
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
    slen = str->sbuf.length;
    string = nn_util_strtoupper(str->sbuf.data, slen);
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
    slen = str->sbuf.length;
    string = nn_util_strtolower(str->sbuf.data, slen);
    return nn_value_fromobject(nn_string_copylen(state, string, slen));
}

NNValue nn_objfnstring_isalpha(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    size_t i;
    NNArgCheck check;
    NNObjString* selfstr;
    nn_argcheck_init(state, &check, "isAlpha", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    selfstr = nn_value_asstring(thisval);
    for(i = 0; i < selfstr->sbuf.length; i++)
    {
        if(!isalpha((unsigned char)selfstr->sbuf.data[i]))
        {
            return nn_value_makebool(false);
        }
    }
    return nn_value_makebool(selfstr->sbuf.length != 0);
}

NNValue nn_objfnstring_isalnum(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    size_t i;
    NNObjString* selfstr;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "isAlnum", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    selfstr = nn_value_asstring(thisval);
    for(i = 0; i < selfstr->sbuf.length; i++)
    {
        if(!isalnum((unsigned char)selfstr->sbuf.data[i]))
        {
            return nn_value_makebool(false);
        }
    }
    return nn_value_makebool(selfstr->sbuf.length != 0);
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
    if(selfstr->sbuf.length ==0)
    {
        return nn_value_makebool(false);
    }
    f = strtod(selfstr->sbuf.data, &p);
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
    NNObjString* selfstr;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "isNumber", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    selfstr = nn_value_asstring(thisval);
    for(i = 0; i < selfstr->sbuf.length; i++)
    {
        if(!isdigit((unsigned char)selfstr->sbuf.data[i]))
        {
            return nn_value_makebool(false);
        }
    }
    return nn_value_makebool(selfstr->sbuf.length != 0);
}

NNValue nn_objfnstring_islower(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    size_t i;
    bool alphafound;
    NNObjString* selfstr;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "isLower", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    selfstr = nn_value_asstring(thisval);
    alphafound = false;
    for(i = 0; i < selfstr->sbuf.length; i++)
    {
        if(!alphafound && !isdigit(selfstr->sbuf.data[0]))
        {
            alphafound = true;
        }
        if(isupper(selfstr->sbuf.data[0]))
        {
            return nn_value_makebool(false);
        }
    }
    return nn_value_makebool(alphafound);
}

NNValue nn_objfnstring_isupper(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    size_t i;
    bool alphafound;
    NNObjString* selfstr;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "isUpper", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    selfstr = nn_value_asstring(thisval);
    alphafound = false;
    for(i = 0; i < selfstr->sbuf.length; i++)
    {
        if(!alphafound && !isdigit(selfstr->sbuf.data[0]))
        {
            alphafound = true;
        }
        if(islower(selfstr->sbuf.data[0]))
        {
            return nn_value_makebool(false);
        }
    }
    return nn_value_makebool(alphafound);
}

NNValue nn_objfnstring_isspace(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    size_t i;
    NNObjString* selfstr;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "isSpace", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    selfstr = nn_value_asstring(thisval);
    for(i = 0; i < selfstr->sbuf.length; i++)
    {
        if(!isspace((unsigned char)selfstr->sbuf.data[i]))
        {
            return nn_value_makebool(false);
        }
    }
    return nn_value_makebool(selfstr->sbuf.length != 0);
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
        trimmer = (char)nn_value_asstring(argv[0])->sbuf.data[0];
    }
    selfstr = nn_value_asstring(thisval);
    string = selfstr->sbuf.data;
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
    char* end;
    char* string;
    char trimmer;
    NNObjString* selfstr;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "ltrim", argv, argc);
    NEON_ARGS_CHECKCOUNTRANGE(&check, 0, 1);
    trimmer = '\0';
    if(argc == 1)
    {
        trimmer = (char)nn_value_asstring(argv[0])->sbuf.data[0];
    }
    selfstr = nn_value_asstring(thisval);
    string = selfstr->sbuf.data;
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
    char* end;
    char* string;
    char trimmer;
    NNObjString* selfstr;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "rtrim", argv, argc);
    NEON_ARGS_CHECKCOUNTRANGE(&check, 0, 1);
    trimmer = '\0';
    if(argc == 1)
    {
        trimmer = (char)nn_value_asstring(argv[0])->sbuf.data[0];
    }
    selfstr = nn_value_asstring(thisval);
    string = selfstr->sbuf.data;
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
    char* haystack;
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
    if(string->sbuf.length > 0 && needle->sbuf.length > 0)
    {
        haystack = string->sbuf.data;
        result = strstr(haystack + startindex, needle->sbuf.data);
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
    if(string->sbuf.length == 0 || substr->sbuf.length == 0 || substr->sbuf.length > string->sbuf.length)
    {
        return nn_value_makebool(false);
    }
    return nn_value_makebool(memcmp(substr->sbuf.data, string->sbuf.data, substr->sbuf.length) == 0);
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
    if(string->sbuf.length == 0 || substr->sbuf.length == 0 || substr->sbuf.length > string->sbuf.length)
    {
        return nn_value_makebool(false);
    }
    difference = string->sbuf.length - substr->sbuf.length;
    return nn_value_makebool(memcmp(substr->sbuf.data, string->sbuf.data + difference, substr->sbuf.length) == 0);
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
    prc = mrx_regex_parse(&pctx, pattern->sbuf.data, 0);
    if(prc == 0)
    {
        cpres = mrx_regex_match(&pctx, string->sbuf.data, 0, actualmaxcaptures, capstarts, caplengths);
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
                        strstart = &string->sbuf.data[mtstart];
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
    if(substr->sbuf.length == 0 || string->sbuf.length == 0)
    {
        return nn_value_makenumber(0);
    }
    count = 0;
    tmp = string->sbuf.data;
    while((tmp = nn_util_utf8strstr(tmp, substr->sbuf.data)))
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
    return nn_value_makenumber(strtod(selfstr->sbuf.data, NULL));
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
    length = string->sbuf.length;
    if(length > 0)
    {
        for(i = 0; i < length; i++)
        {
            start = i;
            end = i + 1;
            nn_array_push(list, nn_value_fromobject(nn_string_copylen(state, string->sbuf.data + start, (int)(end - start))));
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
    size_t finalutf8size;
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
        fillchar = ofillstr->sbuf.data[0];
    }
    if(width <= string->sbuf.length)
    {
        return thisval;
    }
    fillsize = width - string->sbuf.length;
    fill = (char*)nn_memory_malloc(sizeof(char) * ((size_t)fillsize + 1));
    finalsize = string->sbuf.length + fillsize;
    finalutf8size = string->sbuf.length + fillsize;
    for(i = 0; i < fillsize; i++)
    {
        fill[i] = fillchar;
    }
    str = (char*)nn_memory_malloc(sizeof(char) * ((size_t)finalsize + 1));
    memcpy(str, fill, fillsize);
    memcpy(str + fillsize, string->sbuf.data, string->sbuf.length);
    str[finalsize] = '\0';
    nn_memory_free(fill);
    result = nn_string_takelen(state, str, finalsize);
    result->sbuf.length = finalutf8size;
    result->sbuf.length = finalsize;
    return nn_value_fromobject(result);
}

NNValue nn_objfnstring_rpad(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    size_t i;
    size_t width;
    size_t fillsize;
    size_t finalsize;
    size_t finalutf8size;
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
        fillchar = ofillstr->sbuf.data[0];
    }
    if(width <= string->sbuf.length)
    {
        return thisval;
    }
    fillsize = width - string->sbuf.length;
    fill = (char*)nn_memory_malloc(sizeof(char) * ((size_t)fillsize + 1));
    finalsize = string->sbuf.length + fillsize;
    finalutf8size = string->sbuf.length + fillsize;
    for(i = 0; i < fillsize; i++)
    {
        fill[i] = fillchar;
    }
    str = (char*)nn_memory_malloc(sizeof(char) * ((size_t)finalsize + 1));
    memcpy(str, string->sbuf.data, string->sbuf.length);
    memcpy(str + string->sbuf.length, fill, fillsize);
    str[finalsize] = '\0';
    nn_memory_free(fill);
    result = nn_string_takelen(state, str, finalsize);
    result->sbuf.length = finalutf8size;
    result->sbuf.length = finalsize;
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
    if(((string->sbuf.length == 0) && (delimeter->sbuf.length == 0)) || (string->sbuf.length == 0) || (delimeter->sbuf.length == 0))
    {
        return nn_value_fromobject(nn_object_makearray(state));
    }
    list = (NNObjArray*)nn_gcmem_protect(state, (NNObject*)nn_object_makearray(state));
    if(delimeter->sbuf.length > 0)
    {
        start = 0;
        for(i = 0; i <= string->sbuf.length; i++)
        {
            /* match found. */
            if(memcmp(string->sbuf.data + i, delimeter->sbuf.data, delimeter->sbuf.length) == 0 || i == string->sbuf.length)
            {
                nn_array_push(list, nn_value_fromobject(nn_string_copylen(state, string->sbuf.data + start, i - start)));
                i += delimeter->sbuf.length - 1;
                start = i + 1;
            }
        }
    }
    else
    {
        length = string->sbuf.length;
        for(i = 0; i < length; i++)
        {
            start = i;
            end = i + 1;
            nn_array_push(list, nn_value_fromobject(nn_string_copylen(state, string->sbuf.data + start, (int)(end - start))));
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
    if((string->sbuf.length == 0 && substr->sbuf.length == 0) || string->sbuf.length == 0 || substr->sbuf.length == 0)
    {
        return nn_value_fromobject(nn_string_copylen(state, string->sbuf.data, string->sbuf.length));
    }
    result = nn_strbuf_makebasicempty(0);
    totallength = 0;
    for(i = 0; i < string->sbuf.length; i++)
    {
        if(memcmp(string->sbuf.data + i, substr->sbuf.data, substr->sbuf.length) == 0)
        {
            if(substr->sbuf.length > 0)
            {
                nn_strbuf_appendstrn(result, repsubstr->sbuf.data, repsubstr->sbuf.length);
            }
            i += substr->sbuf.length - 1;
            totallength += repsubstr->sbuf.length;
        }
        else
        {
            nn_strbuf_appendchar(result, string->sbuf.data[i]);
            totallength++;
        }
    }
    return nn_value_fromobject(nn_string_makefromstrbuf(state, result, nn_util_hashstring(result->data, result->length)));
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
    length = string->sbuf.length;
    index = nn_value_asnumber(argv[0]);
    if(((int)index > -1) && (index < length))
    {
        result = nn_string_copylen(state, &string->sbuf.data[index], 1);
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
    length = string->sbuf.length;
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
    for(i = 0; i < string->sbuf.length; i++)
    {
        passi = 0;
        if(arity > 0)
        {
            passi++;
            nestargs[0] = nn_value_fromobject(nn_string_copylen(state, string->sbuf.data + i, 1));
            if(arity > 1)
            {
                passi++;
                nestargs[1] = nn_value_makenumber(i);
            }
        }
        nn_nestcall_callfunction(state, callable, thisval, nestargs, passi, &unused);
    }
    /* pop the argument list */
    return nn_value_makenull();
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

