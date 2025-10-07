
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
#define nn_strbuf_boundscheckinsert(sb, pos) nn_strutil_callboundscheckinsert(sb, pos, __FILE__, __LINE__)
#define nn_strbuf_boundscheckreadrange(sb, start, len) nn_strutil_callboundscheckreadrange(sb, start, len, __FILE__, __LINE__)


#define ROUNDUP2POW(x) nn_strutil_rndup2pow64(x)

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

void nn_strutil_callboundscheckinsert(NNStringBuffer* sb, size_t pos, const char* file, int line)
{
    if(pos > sb->storedlength)
    {
        fprintf(stderr, "%s:%i: - out of bounds error [index: %ld, num_of_bits: %ld]\n", file, line, (long)pos, (long)sb->storedlength);
        errno = EDOM;
        nn_strbuf_exitonerror();
    }
}

/* Bounds check when reading a range (start+len < strlen is valid) */
void nn_strutil_callboundscheckreadrange(NNStringBuffer* sb, size_t start, size_t len, const char* file, int line)
{
    if(start + len > sb->storedlength)
    {
        fprintf(stderr,"%s:%i: - out of bounds error [start: %ld; length: %ld; strlen: %ld; buf:%.*s%s]\n",
                file, line, (long)start, (long)len, (long)sb->storedlength, (int)STRBUF_MIN(5, sb->storedlength), nn_strbuf_data(sb), sb->storedlength > 5 ? "..." : "");
        errno = EDOM;
        nn_strbuf_exitonerror();
    }
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
    buff = (char*)nn_memory_malloc((i + oldcount * (sublen - findlen) + 1) * sizeof(char));
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
    *str = (char*)nn_memory_malloc((i + 1) * sizeof(char));
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


NNStringBuffer* nn_strbuf_makelongfromptr(NNStringBuffer* sb, size_t len)
{
    //fprintf(stderr, "in makelong...\n");
    sb->isintern = false;
    sb->storedlength = 0;
    sb->capacity = ROUNDUP2POW(len + 1);
    sb->longstrdata = (char*)nn_memory_malloc(sb->capacity);
    if(!sb->longstrdata)
    {
        return NULL;
    }
    sb->longstrdata[0] = '\0';
    return sb;
}

bool nn_strbuf_initbasicempty(NNStringBuffer* sb, const char* str, size_t len, bool onstack)
{
    memset(sb, 0, sizeof(NNStringBuffer));
    sb->isintern = false;
    sb->capacity = len;
    sb->storedlength = 0;
    sb->longstrdata = NULL;
    if(len > 0)
    {
        sb->longstrdata = NULL;
        if(!nn_strbuf_makelongfromptr(sb, len))
        {
            if(!onstack)
            {
                nn_memory_free(sb);
            }
            return false;
        }
    }
    if((str != NULL) && (len > 0))
    {
        return nn_strbuf_appendstrn(sb, str, len);
    }
    return true;
}

bool nn_strbuf_makebasicemptystack(NNStringBuffer* sb, const char* str, size_t len)
{
    return nn_strbuf_initbasicempty(sb, str, len, true);
}

NNStringBuffer* nn_strbuf_makebasicempty(const char* str, size_t len)
{
    NNStringBuffer* sb;
    sb = (NNStringBuffer*)nn_memory_malloc(sizeof(NNStringBuffer));
    if(!sb)
    {
        return NULL;
    }
    if(!nn_strbuf_initbasicempty(sb, str, len, false))
    {
        return NULL;
    }
    return sb;
}

bool nn_strbuf_destroyfromstack(NNStringBuffer* sb)
{
    if(!sb->isintern)
    {
        nn_memory_free(sb->longstrdata);
    }
    return true;
}

bool nn_strbuf_destroy(NNStringBuffer* sb)
{
    nn_strbuf_destroyfromstack(sb);
    nn_memory_free(sb);
    return true;
}


/* Clear the content of an existing NNStringBuffer (sets size to 0) */
void nn_strbuf_reset(NNStringBuffer* sb)
{
    if(sb->longstrdata)
    {
        memset(sb->longstrdata, 0, sb->storedlength);
    }
    sb->storedlength = 0;
}


/* Ensure capacity for len characters plus '\0' character - exits on FAILURE */
bool nn_strbuf_ensurecapacity(NNStringBuffer* sb, size_t len)
{
    bool mustcopy;
    char* ptr;
    char* tmpbuf;
    mustcopy = false;
    tmpbuf = NULL;

    /* for nul byte */
    len++;
    if((sb->capacity == 0) || (sb->capacity < len))
    {
        sb->capacity = ROUNDUP2POW(len);
        /* fprintf(stderr, "sizeptr=%ld\n", sb->capacity); */
        if(mustcopy /*|| sb->longstrdata == NULL*/)
        {
            ptr = (char*)nn_memory_malloc(sb->capacity);
        }
        else
        {
            ptr = (char*)nn_memory_realloc(sb->longstrdata, sb->capacity);
        }
        if(ptr == NULL)
        {
            fprintf(stderr, "[%s:%i] Out of memory\n", __FILE__, __LINE__);
            return false;
        }
        if(mustcopy)
        {
            //fprintf(stderr, "ensurecapacity: copying from short ((%d) <<%.*s>>)\n", (int)sb->storedlength, (int)sb->storedlength, tmpbuf);
            memcpy(ptr, tmpbuf, sb->storedlength);
        }
        sb->longstrdata = ptr;
    }
    return true;
}

/*
// Resize the buffer to have capacity to hold a string of length newlen
// (+ a null terminating character).  Can also be used to downsize the buffer's
// memory usage.  Returns 1 on success, 0 on failure.
*/
bool nn_strbuf_resize(NNStringBuffer* sb, size_t newlen)
{
    return nn_strbuf_ensurecapacity(sb, newlen);
}

bool nn_strbuf_setlength(NNStringBuffer* sb, size_t len)
{
    sb->storedlength  = len;
    return true;
}

bool nn_strbuf_setdata(NNStringBuffer* sb, char* str)
{
    sb->longstrdata = str;
    return true;
}

size_t nn_strbuf_length(NNStringBuffer* sb)
{
    return sb->storedlength;
}

const char* nn_strbuf_data(NNStringBuffer* sb)
{
    return sb->longstrdata;
}

#define nn_strbuf_mutdata(sb) \
    ( \
        (sb)->longstrdata \
    )

int nn_strbuf_get(NNStringBuffer* sb, size_t idx)
{
    return sb->longstrdata[idx];
}

bool nn_strbuf_containschar(NNStringBuffer* sb, char ch)
{
    size_t i;
    const char* data;
    data = nn_strbuf_data(sb);
    for(i=0; i<sb->storedlength; i++)
    {
        if(data[i] == ch)
        {
            return true;
        }
    }
    return false;
}

bool nn_strbuf_fullreplace(NNStringBuffer* sb, const char* findstr, size_t findlen, const char* substr, size_t sublen)
{
    size_t nl;
    size_t needed;
    char* data;
    data = nn_strbuf_mutdata(sb);
    needed = nn_strutil_strrepcount(data, sb->storedlength, findstr, findlen, sublen);
    if(needed == 0)
    {
        return false;
    }
    nn_strbuf_ensurecapacity(sb, sb->capacity + needed);
    data = nn_strbuf_mutdata(sb);
    nl = nn_strutil_strreplace1(&data, sb->storedlength, findstr, findlen, substr, sublen);
    sb->storedlength = nl;
    return true;
}

bool nn_strbuf_charreplace(NNStringBuffer* sb, int findme, const char* substr, size_t sublen)
{
    size_t i;
    size_t nlen;
    size_t needed;
    char* data;
    needed = sb->capacity;
    data = nn_strbuf_mutdata(sb);
    for(i=0; i<sb->storedlength; i++)
    {
        if(data[i] == findme)
        {
            needed += sublen;
        }
    }
    if(!nn_strbuf_ensurecapacity(sb, needed+1))
    {
        return false;
    }
    data = nn_strbuf_mutdata(sb);
    nlen = nn_strutil_inpreplace(data, sb->storedlength, findme, substr, sublen, sb->capacity);
    sb->storedlength = nlen;
    return true;
}


/* Set string buffer to contain a given string */
bool nn_strbuf_set(NNStringBuffer* sb, size_t idx, int b)
{
    char* data;
    nn_strbuf_ensurecapacity(sb, idx);
    data = nn_strbuf_mutdata(sb);
    data[idx] = b;
    return true;
}

/* Add a character to the end of this NNStringBuffer */
bool nn_strbuf_appendchar(NNStringBuffer* sb, int c)
{
    char* data;
    nn_strbuf_ensurecapacity(sb, sb->storedlength + 1);
    data = nn_strbuf_mutdata(sb);
    data[sb->storedlength] = c;
    data[sb->storedlength + 1] = '\0';
    sb->storedlength++;
    return true;
}

/*
// Copy N characters from a character array to the end of this NNStringBuffer
// strlen(str) must be >= len
*/
bool nn_strbuf_appendstrn(NNStringBuffer* sb, const char* str, size_t len)
{
    size_t i;
    int epos;
    char* data;
    epos = 0;
    if(len > 0)
    {
        nn_strbuf_ensurecapacity(sb, sb->storedlength + len);
        data = nn_strbuf_mutdata(sb);
        if(sb->storedlength > 0)
        {
            epos = sb->storedlength;
        }
        memcpy(data + epos, str, len);
        sb->storedlength = sb->storedlength + len;
        data[sb->storedlength] = '\0';
    }
    return true;
}

/* Copy a character array to the end of this NNStringBuf^^fer */
bool nn_strbuf_appendstr(NNStringBuffer* sb, const char* str)
{
    return nn_strbuf_appendstrn(sb, str, strlen(str));
}

bool nn_strbuf_appendbuff(NNStringBuffer* sb1, NNStringBuffer* sb2)
{
    return nn_strbuf_appendstrn(sb1, nn_strbuf_data(sb2), sb2->storedlength);
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
bool nn_strbuf_appendnumulong(NNStringBuffer* sb, unsigned long value)
{
    size_t v;
    size_t pos;
    size_t numdigits;
    char* dst;
    char* data;
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
    nn_strbuf_ensurecapacity(sb, sb->storedlength + numdigits);
    data = nn_strbuf_mutdata(sb);
    dst = data + sb->storedlength;
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
    sb->storedlength += numdigits;
    data[sb->storedlength] = '\0';
    return true;
}

bool nn_strbuf_appendnumlong(NNStringBuffer* sb, long value)
{
    /* nn_strbuf_appendformat(sb, "%li", value); */
    if(value < 0)
    {
        nn_strbuf_appendchar(sb, '-');
        value = -value;
    }
    return nn_strbuf_appendnumulong(sb, value);
}

bool nn_strbuf_appendnumint(NNStringBuffer* sb, int value)
{
    /* nn_strbuf_appendformat(sb, "%i", value); */
    return nn_strbuf_appendnumlong(sb, value);
}

/* Append string converted to lowercase */
bool nn_strbuf_appendstrnlowercase(NNStringBuffer* sb, const char* str, size_t len)
{
    char* to;
    char* data;
    const char* plength;
    nn_strbuf_ensurecapacity(sb, sb->storedlength + len);
    data = nn_strbuf_mutdata(sb);
    to = data + sb->storedlength;
    plength = str + len;
    for(; str < plength; str++, to++)
    {
        *to = tolower(*str);
    }
    sb->storedlength += len;
    data[sb->storedlength] = '\0';
    return true;
}

/* Append string converted to uppercase */
bool nn_strbuf_appendstrnuppercase(NNStringBuffer* sb, const char* str, size_t len)
{
    char* to;
    char* data;
    const char* end;
    nn_strbuf_ensurecapacity(sb, sb->storedlength + len);
    data = nn_strbuf_mutdata(sb);
    to = data + sb->storedlength;
    end = str + len;
    for(; str < end; str++, to++)
    {
        *to = toupper(*str);
    }
    sb->storedlength += len;
    data[sb->storedlength] = '\0';
    return true;
}

void nn_strbuf_shrink(NNStringBuffer* sb, size_t len)
{
    char* data;
    data = nn_strbuf_mutdata(sb);
    data[len] = 0;
    sb->storedlength = len;
}

/*
// Remove \r and \n characters from the end of this StringBuffesr
// Returns the number of characters removed
*/
size_t nn_strbuf_chomp(NNStringBuffer* sb)
{
    size_t oldlen;
    char* data;
    data = nn_strbuf_mutdata(sb);
    oldlen = sb->storedlength;
    sb->storedlength = nn_strutil_chomp(data, sb->storedlength);
    return oldlen - sb->storedlength;
}

/* Reverse a string */
void nn_strbuf_reverse(NNStringBuffer* sb)
{
    char* data;
    data = nn_strbuf_mutdata(sb);
    nn_strutil_reverseregion(data, sb->storedlength);
}

/*
// Get a substring as a new null terminated char array
// (remember to free the returned char* after you're done with it!)
*/
char* nn_strbuf_substr(NNStringBuffer* sb, size_t start, size_t len)
{
    char* data;
    char* newstr;
    nn_strbuf_boundscheckreadrange(sb, start, len);
    data = nn_strbuf_mutdata(sb);
    newstr = (char*)nn_memory_malloc((len + 1) * sizeof(char));
    strncpy(newstr, data + start, len);
    newstr[len] = '\0';
    return newstr;
}

void nn_strbuf_touppercase(NNStringBuffer* sb)
{
    char* pos;
    char* end;
    char* data;
    data = nn_strbuf_mutdata(sb);
    end = data + sb->storedlength;
    for(pos = data; pos < end; pos++)
    {
        *pos = (char)toupper(*pos);
    }
}

void nn_strbuf_tolowercase(NNStringBuffer* sb)
{
    char* pos;
    char* end;
    char* data;
    data = nn_strbuf_mutdata(sb);
    end = data + sb->storedlength;
    for(pos = data; pos < end; pos++)
    {
        *pos = (char)tolower(*pos);
    }
}

/*
// Copy a string to this NNStringBuffer, overwriting any existing characters
// Note: dstpos + len can be longer the the current sb NNStringBuffer
*/
void nn_strbuf_copyover(NNStringBuffer* sb, size_t dstpos, const char* src, size_t len)
{
    size_t newlen;
    char* data;
    if(src == NULL || len == 0)
    {
        return;
    }
    nn_strbuf_boundscheckinsert(sb, dstpos);
    /*
    // Check if sb buffer can handle string
    // src may have pointed to sb, which has now moved
    */
    newlen = STRBUF_MAX(dstpos + len, sb->storedlength);
    nn_strbuf_ensurecapacity(sb, newlen);
    data = nn_strbuf_mutdata(sb);
    /* memmove instead of strncpy, as it can handle overlapping regions */
    memmove(data + dstpos, src, len * sizeof(char));
    if(dstpos + len > sb->storedlength)
    {
        /* Extended string - add '\0' char */
        sb->storedlength = dstpos + len;
        data[sb->storedlength] = '\0';
    }
}

/* Insert: copy to a NNStringBuffer, shifting any existing characters along */
void nn_strbuf_insert(NNStringBuffer* sb, size_t dstpos, const char* src, size_t len)
{
    char* data;
    char* insert;
    if(src == NULL || len == 0)
    {
        return;
    }
    nn_strbuf_boundscheckinsert(sb, dstpos);
    /*
    // Check if sb buffer has capacity for inserted string plus \0
    // src may have pointed to sb, which will be moved in realloc when
    // calling ensure capacity
    */
    nn_strbuf_ensurecapacity(sb, sb->storedlength + len);
    data = nn_strbuf_mutdata(sb);
    insert = data + dstpos;
    /* dstpos could be at the end (== sb->storedlength) */
    if(dstpos < sb->storedlength)
    {
        /* Shift some characters up */
        memmove(insert + len, insert, (sb->storedlength - dstpos) * sizeof(char));
        if(src >= data && src < data + sb->capacity)
        {
            /* src/sb strings point to the same string in memory */
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
    sb->storedlength += len;
    data[sb->storedlength] = '\0';
}

/*
// Overwrite dstpos..(dstpos+dstlen-1) with srclen chars from src
// if dstlen != srclen, content to the right of dstlen is shifted
// Example:
//   nn_strbuf_set(sb, "aaabbccc");
//   char *mystr = "xxx";
//   nn_strbuf_overwrite(sb,3,2,mystr,strlen(mystr));
//   // sb is now "aaaxxxccc"
//   nn_strbuf_overwrite(sb,3,2,"_",1);
//   // sb is now "aaa_ccc"
*/
void nn_strbuf_overwrite(NNStringBuffer* sb, size_t dstpos, size_t dstlen, const char* src, size_t srclen)
{
    size_t len;
    size_t newlen;
    char* tgt;
    char* end;
    char* data;
    nn_strbuf_boundscheckreadrange(sb, dstpos, dstlen);
    if(src == NULL)
    {
        return;
    }
    if(dstlen == srclen)
    {
        nn_strbuf_copyover(sb, dstpos, src, srclen);
    }
    newlen = sb->storedlength + srclen - dstlen;
    nn_strbuf_ensurecapacity(sb, newlen);
    data = nn_strbuf_mutdata(sb);
    if(src >= data && src < data + sb->capacity)
    {
        if(srclen < dstlen)
        {
            /* copy */
            memmove(data + dstpos, src, srclen * sizeof(char));
            /* resize (shrink) */
            memmove(data + dstpos + srclen, data + dstpos + dstlen, (sb->storedlength - dstpos - dstlen) * sizeof(char));
        }
        else
        {
            /*
            // Buffer is going to grow and src points to this buffer
            // resize (grow)
            */
            memmove(data + dstpos + srclen, data + dstpos + dstlen, (sb->storedlength - dstpos - dstlen) * sizeof(char));
            tgt = data + dstpos;
            end = data + dstpos + srclen;
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
        memmove(data + dstpos + srclen, data + dstpos + dstlen, (sb->storedlength - dstpos - dstlen) * sizeof(char));
        /* copy */
        memcpy(data + dstpos, src, srclen * sizeof(char));
    }
    sb->storedlength = newlen;
    data[sb->storedlength] = '\0';
}

/*
// Remove characters from the buffer
//   nn_strbuf_set(sb, "aaaBBccc");
//   nn_strbuf_erase(sb, 3, 2);
//   // sb is now "aaaccc"
*/
void nn_strbuf_erase(NNStringBuffer* sb, size_t pos, size_t len)
{
    char* data;
    nn_strbuf_boundscheckreadrange(sb, pos, len);
    data = nn_strbuf_mutdata(sb);
    memmove(data + pos, data + pos + len, sb->storedlength - pos - len);
    sb->storedlength -= len;
    data[sb->storedlength] = '\0';
}

/*
// sprintf
*/

int nn_strbuf_appendformatposv(NNStringBuffer* sb, size_t pos, const char* fmt, va_list argptr)
{
    size_t buflen;
    int numchars;
    va_list vacpy;
    char* data;
    nn_strbuf_boundscheckinsert(sb, pos);
    /* Length of remaining buffer */
    buflen = sb->capacity - pos;
    if(buflen == 0 && !nn_strbuf_ensurecapacity(sb, sb->capacity << 1))
    {
        fprintf(stderr, "%s:%i:Error: Out of memory\n", __FILE__, __LINE__);
        nn_strbuf_exitonerror();
    }
    data = nn_strbuf_mutdata(sb);
    /* Make a copy of the list of args incase we need to resize buff and try again */
    va_copy(vacpy, argptr);
    numchars = vsnprintf(data + pos, buflen, fmt, argptr);
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
        nn_strbuf_ensurecapacity(sb, pos + (size_t)numchars);
        /*
        // now use the argptr copy we made earlier
        // Don't need to use vsnprintf now, vsprintf will do since we know it'll fit
        */
        data = nn_strbuf_mutdata(sb);
        numchars = vsprintf(data + pos, fmt, vacpy);
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
    sb->storedlength = pos + (size_t)numchars;
    return numchars;
}

int nn_strbuf_appendformatv(NNStringBuffer* sb, const char* fmt, va_list argptr)
{
    return nn_strbuf_appendformatposv(sb, sb->storedlength, fmt, argptr);
}

/* sprintf to the end of a NNStringBuffer (adds string terminator after sprint) */
int nn_strbuf_appendformat(NNStringBuffer* sb, const char* fmt, ...)
{
    int numchars;
    va_list argptr;
    va_start(argptr, fmt);
    numchars = nn_strbuf_appendformatposv(sb, sb->storedlength, fmt, argptr);
    va_end(argptr);
    return numchars;
}

/* Print at a given position (overwrite chars at positions >= pos) */
int nn_strbuf_appendformatat(NNStringBuffer* sb, size_t pos, const char* fmt, ...)
{
    int numchars;
    va_list argptr;
    nn_strbuf_boundscheckinsert(sb, pos);
    va_start(argptr, fmt);
    numchars = nn_strbuf_appendformatposv(sb, pos, fmt, argptr);
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
int nn_strbuf_appendformatnoterm(NNStringBuffer* sb, size_t pos, const char* fmt, ...)
{
    size_t len;
    int nchars;
    char lastchar;
    va_list argptr;
    char* data;
    nn_strbuf_boundscheckinsert(sb, pos);
    len = sb->storedlength;
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
    data = nn_strbuf_mutdata(sb);
    lastchar = (pos + (size_t)nchars < sb->storedlength) ? data[pos + (size_t)nchars] : 0;
    va_start(argptr, fmt);
    nchars = nn_strbuf_appendformatposv(sb, pos, fmt, argptr);
    va_end(argptr);
    if(nchars < 0)
    {
        fprintf(stderr, "Warning: nn_strbuf_appendformatv something went wrong..\n");
        nn_strbuf_exitonerror();
    }
    /* Restore length if shrunk, null terminate if extended */
    if(sb->storedlength < len)
    {
        sb->storedlength = len;
    }
    else
    {
        data[sb->storedlength] = '\0';
    }
    /* Re-instate overwritten character */
    data[pos + (size_t)nchars] = lastchar;
    return nchars;
}

/* Trim whitespace characters from the start and end of a string */
void nn_strbuf_triminplace(NNStringBuffer* sb)
{
    size_t start;
    char* data;
    if(sb->storedlength == 0)
    {
        return;
    }
    data = nn_strbuf_mutdata(sb);
    /* Trim end first */
    while(sb->storedlength > 0 && isspace((int)data[sb->storedlength - 1]))
    {
        sb->storedlength--;
    }
    data[sb->storedlength] = '\0';
    if(sb->storedlength == 0)
    {
        return;
    }
    start = 0;
    while(start < sb->storedlength && isspace((int)data[start]))
    {
        start++;
    }
    if(start != 0)
    {
        sb->storedlength -= start;
        memmove(data, data + start, sb->storedlength * sizeof(char));
        data[sb->storedlength] = '\0';
    }
}

/*
// Trim the characters listed in `list` from the left of `sb`
// `list` is a null-terminated string of characters
*/
void nn_strbuf_trimleftinplace(NNStringBuffer* sb, const char* list)
{
    size_t start;
    char* data;
    start = 0;
    data = nn_strbuf_mutdata(sb);
    while(start < sb->storedlength && (strchr(list, data[start]) != NULL))
    {
        start++;
    }
    if(start != 0)
    {
        sb->storedlength -= start;
        memmove(data, data + start, sb->storedlength * sizeof(char));
        data[sb->storedlength] = '\0';
    }
}

/*
// Trim the characters listed in `list` from the right of `sb`
// `list` is a null-terminated string of characters
*/
void nn_strbuf_trimrightinplace(NNStringBuffer* sb, const char* list)
{
    char* data;
    if(sb->storedlength == 0)
    {
        return;
    }
    data = nn_strbuf_mutdata(sb);
    while(sb->storedlength > 0 && strchr(list, data[sb->storedlength - 1]) != NULL)
    {
        sb->storedlength--;
    }
    data[sb->storedlength] = '\0';
}

