
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <ctype.h>
#include <assert.h>
#include "strbuf.h"
#include "mem.h"

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

#define dyn_strbuf_exitonerror()     \
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
#define dyn_strbuf_boundscheckinsert(sbuf, pos) dyn_strutil_callboundscheckinsert(sbuf, pos, __FILE__, __LINE__)
#define dyn_strbuf_boundscheckreadrange(sbuf, start, len) dyn_strutil_callboundscheckreadrange(sbuf, start, len, __FILE__, __LINE__)


#define ROUNDUP2POW(x) dyn_strutil_rndup2pow64(x)

size_t dyn_strutil_rndup2pow64(uint64_t x)
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
// `n` is the maximum number of bytes to copy including the NULL byte
// copies at most n bytes from `src` to `dst`
// Always appends a NULL terminating byte, unless n is zero.
// Returns a pointer to dst
*/
char* dyn_strutil_safencpy(char* dst, const char* src, size_t n)
{
    if(n == 0)
    {
        return dst;
    }
    /*
    // From The Open Group:
    //   The memccpy() function copies bytes from memory area s2 into s1, stopping
    //   after the first occurrence of byte c is copied, or after n bytes are copied,
    //   whichever comes first. If copying takes place between objects that overlap,
    //   the behaviour is undefined.
    // Returns NULL if character c was not found in the copied memory
    */
    if(memccpy(dst, src, '\0', n - 1) == NULL)
    {
        dst[n - 1] = '\0';
    }
    return dst;
}

/*
// Replaces `sep` with \0 in str
// Returns number of occurances of `sep` character in `str`
// Stores `nptrs` pointers in `ptrs`
*/
size_t dyn_strutil_splitstr(char* str, char sep, char** ptrs, size_t nptrs)
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
size_t dyn_strutil_charreplace(char* str, char from, char to)
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
void dyn_strutil_reverseregion(char* str, size_t length)
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

bool dyn_strutil_isallspace(const char* s)
{
    int i;
    for(i = 0; s[i] != '\0' && isspace((int)s[i]); i++)
    {
    }
    return (s[i] == '\0');
}

char* dyn_strutil_nextspace(char* s)
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
char* dyn_strutil_trim(char* str)
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
size_t dyn_strutil_chomp(char* str, size_t len)
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
size_t dyn_strutil_countchar(const char* str, char c)
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
size_t dyn_strutil_split(const char* splitat, const char* sourcetxt, char*** result)
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

void dyn_strutil_callboundscheckinsert(const StringBuffer* sbuf, size_t pos, const char* file, int line)
{
    if(pos > sbuf->length)
    {
        fprintf(stderr, "%s:%i: - out of bounds error [index: %ld, num_of_bits: %ld]\n", file, line, (long)pos, (long)sbuf->length);
        errno = EDOM;
        dyn_strbuf_exitonerror();
    }
}

/* Bounds check when reading a range (start+len < strlen is valid) */
void dyn_strutil_callboundscheckreadrange(const StringBuffer* sbuf, size_t start, size_t len, const char* file, int line)
{
    if(start + len > sbuf->length)
    {
        fprintf(stderr,"%s:%i: - out of bounds error [start: %ld; length: %ld; strlen: %ld; buf:%.*s%s]\n",
                file, line, (long)start, (long)len, (long)sbuf->length, (int)STRBUF_MIN(5, sbuf->length), sbuf->data, sbuf->length > 5 ? "..." : "");
        errno = EDOM;
        dyn_strbuf_exitonerror();
    }
}

/*
// Constructors / Destructors
*/

void dyn_strbuf_convertfromintern(StringBuffer* sbuf)
{
    char* abuf;
    if(sbuf->isintern)
    {
        fprintf(stderr, "converting stringbuffer from const to dynamic!!\n");
        assert(false);
        abuf = (char*)nn_memory_malloc(sizeof(char) * (sbuf->capacity));
        memcpy(abuf, sbuf->data, sbuf->length);
        /* if sbuf->data is NOT stack memory, it'll be orphaned here!!!!! */
        sbuf->data = abuf;
    }
}

/*
// Place a string buffer into existing memory. Example:
//   StringBuffer buf;
//   dyn_strbuf_makefromptr(&buf, 100);
//   ...
//   dyn_strbuf_destroyfromptr(&buf);
*/
StringBuffer* dyn_strbuf_makefromptr(StringBuffer* sbuf, size_t len)
{
    sbuf->length = 0;
    sbuf->capacity = ROUNDUP2POW(len + 1);
    sbuf->data = (char*)nn_memory_malloc(sbuf->capacity);
    if(!sbuf->data)
    {
        return NULL;
    }
    sbuf->data[0] = '\0';
    return sbuf;
}

bool dyn_strbuf_initbasicempty(StringBuffer* sbuf, size_t len, bool isintern, bool onstack)
{
    sbuf->length = len;
    sbuf->capacity = 0;
    sbuf->data = NULL;
    sbuf->isintern = isintern;
    if(isintern)
    {
        return true;
    }
    if(!dyn_strbuf_makefromptr(sbuf, len))
    {
        if(!onstack)
        {
            nn_memory_free(sbuf);
        }
        return false;
    }
    return true;
}

bool dyn_strbuf_makebasicemptystack(StringBuffer* sbuf, size_t len, bool isintern)
{
    return dyn_strbuf_initbasicempty(sbuf, len, isintern, true);
}

StringBuffer* dyn_strbuf_makebasicempty(size_t len, bool isintern)
{
    StringBuffer* sbuf;
    sbuf = (StringBuffer*)nn_memory_calloc(1, sizeof(StringBuffer));
    if(!sbuf)
    {
        return NULL;
    }
    if(!dyn_strbuf_initbasicempty(sbuf, len, isintern, false))
    {
        return NULL;
    }
    return sbuf;
}

bool dyn_strbuf_destroyfromstack(StringBuffer* sb)
{
    if(!sb->isintern)
    {
        nn_memory_free(sb->data);
    }
    return true;
}

bool dyn_strbuf_destroy(StringBuffer* sb)
{
    if(!sb->isintern)
    {
        nn_memory_free(sb->data);
    }
    nn_memory_free(sb);
    return true;
}

bool dyn_strbuf_destroyfromptr(StringBuffer* sb)
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
StringBuffer* dyn_strbuf_makefromstring(const char* str, size_t slen, bool isintern)
{
    StringBuffer* sbuf;
    sbuf = dyn_strbuf_makebasicempty(slen + 1, isintern);
    if(!sbuf)
    {
        return NULL;
    }
    sbuf->length = slen;
    if(isintern)
    {
        sbuf->data = (char*)str;
    }
    else
    {
        memcpy(sbuf->data, str, slen);
        sbuf->data[sbuf->length] = '\0';
    }
    return sbuf;
}

StringBuffer* dyn_strbuf_makeclone(const StringBuffer* sbuf)
{
    /* One byte for the string end / null char \0 */
    StringBuffer* cpy;
    cpy = dyn_strbuf_makebasicempty(sbuf->length + 1, sbuf->isintern);
    if(!cpy)
    {
        return NULL;
    }
    cpy->length = sbuf->length;
    if(cpy->isintern)
    {
        cpy->data = sbuf->data;
    }
    else
    {
        memcpy(cpy->data, sbuf->data, sbuf->length);
        cpy->data[cpy->length] = '\0';
    }
    return cpy;
}


/* Clear the content of an existing StringBuffer (sets size to 0) */
void dyn_strbuf_reset(StringBuffer* sb)
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
void dyn_strutil_cbufcapacity(char** buf, size_t* sizeptr, size_t len)
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

void dyn_strutil_cbufappendchar(char** buf, size_t* lenptr, size_t* sizeptr, char c)
{
    dyn_strutil_cbufcapacity(buf, sizeptr, *lenptr + 1);
    (*buf)[(*lenptr)++] = c;
    (*buf)[*lenptr] = '\0';
}

/*
// Resize the buffer to have capacity to hold a string of length newlen
// (+ a null terminating character).  Can also be used to downsize the buffer's
// memory usage.  Returns 1 on success, 0 on failure.
*/
bool dyn_strbuf_resize(StringBuffer* sbuf, size_t newlen)
{
    size_t capacity;
    char* newbuf;
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
void dyn_strbuf_ensurecapacity(StringBuffer* sb, size_t len)
{
    dyn_strbuf_convertfromintern(sb);
    dyn_strutil_cbufcapacity(&sb->data, &sb->capacity, len);
}

/* Same as above, but update pointer if it pointed to resized array */
void dyn_strbuf_ensurecapacityupdateptr(StringBuffer* sbuf, size_t size, const char** ptr)
{
    size_t oldcap;
    char* oldbuf;
    if(sbuf->capacity <= size + 1)
    {
        oldcap = sbuf->capacity;
        oldbuf = sbuf->data;
        if(!dyn_strbuf_resize(sbuf, size))
        {
            fprintf(stderr,
                    "%s:%i:Error: _ensure_capacity_update_ptr couldn't resize "
                    "buffer. [requested %ld bytes; capacity: %ld bytes]\n",
                    __FILE__, __LINE__, (long)size, (long)sbuf->capacity);
            dyn_strbuf_exitonerror();
        }
        /* ptr may have pointed to sbuf, which has now moved */
        if(*ptr >= oldbuf && *ptr < oldbuf + oldcap)
        {
            *ptr = sbuf->data + (*ptr - oldbuf);
        }
    }
}

bool dyn_strbuf_containschar(StringBuffer* sb, char ch)
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
void dyn_strutil_faststrncat(char *dest, const char *src, size_t *size)
{
    if(dest && src && size)
    {
        while((dest[*size] = *src++))
        {
            *size += 1;
        }
    }
}

size_t dyn_strutil_strreplace1(char **str, size_t selflen, const char* findstr, size_t findlen, const char *substr, size_t sublen)
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
            dyn_strutil_faststrncat(&buff[i], substr, &x);
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
    dyn_strutil_faststrncat(*str, (const char *)buff, &i);
    nn_memory_free(buff);
    return i;
}

size_t dyn_strutil_strrepcount(const char* str, size_t slen, const char* findstr, size_t findlen, size_t sublen)
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
void dyn_strutil_strreplace2(char* target, size_t tgtlen, const char *findstr, size_t findlen, const char *substr, size_t sublen)
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

bool dyn_strbuf_fullreplace(StringBuffer* sb, const char* findstr, size_t findlen, const char* substr, size_t sublen)
{
    size_t nl;
    size_t needed;
    needed = dyn_strutil_strrepcount(sb->data, sb->length, findstr, findlen, sublen);
    if(needed == 0)
    {
        return false;
    }
    dyn_strbuf_resize(sb, sb->capacity + needed);
    nl = dyn_strutil_strreplace1(&sb->data, sb->length, findstr, findlen, substr, sublen);
    sb->length = nl;
    return true;
}

bool dyn_strutil_inpreplhelper(char *dest, const char *src, size_t srclen, int findme, const char* substr, size_t sublen, size_t maxlen, size_t* dlen)
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
        if(!dyn_strutil_inpreplhelper(dest + sublen, src + 1, srclen, findme, substr, sublen, maxlen - sublen, dlen))
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
        if(!dyn_strutil_inpreplhelper(dest + 1, src + 1, srclen, findme, substr, sublen, maxlen - 1, dlen))
        {
            return false;
        }
    }
    *dest = chatpos;
    return true;
}

size_t dyn_strutil_inpreplace(char* target, size_t tgtlen, int findme, const char* substr, size_t sublen, size_t maxlen)
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
    dyn_strutil_inpreplhelper(target, target, tgtlen, findme, substr, sublen, maxlen - 1, &nlen);
    return nlen;
}

bool dyn_strbuf_charreplace(StringBuffer* sb, int findme, const char* substr, size_t sublen)
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
    if(!dyn_strbuf_resize(sb, needed+1))
    {
        return false;
    }
    nlen = dyn_strutil_inpreplace(sb->data, sb->length, findme, substr, sublen, sb->capacity);
    sb->length = nlen;
    return true;
}

/* Set string buffer to contain a given string */
void dyn_strbuf_set(StringBuffer* sb, const char* str)
{
    size_t len;
    len = strlen(str);
    dyn_strbuf_ensurecapacity(sb, len);
    memcpy(sb->data, str, len);
    sb->data[sb->length = len] = '\0';
}


/* Set string buffer to match existing string buffer */
void dyn_strbuf_setbuff(StringBuffer* dest, StringBuffer* from)
{
    dyn_strbuf_ensurecapacity(dest, from->length);
    memmove(dest->data, from->data, from->length);
    dest->data[dest->length = from->length] = '\0';
}

/* Add a character to the end of this StringBuffer */
bool dyn_strbuf_appendchar(StringBuffer* sb, int c)
{
    dyn_strbuf_ensurecapacity(sb, sb->length + 1);
    sb->data[sb->length] = c;
    sb->data[++sb->length] = '\0';
    return true;
}

/*
// Copy N characters from a character array to the end of this StringBuffer
// strlen(str) must be >= len
*/
bool dyn_strbuf_appendstrn(StringBuffer* sb, const char* str, size_t len)
{
    dyn_strbuf_ensurecapacityupdateptr(sb, sb->length + len, &str);
    memcpy(sb->data + sb->length, str, len);
    sb->data[sb->length = sb->length + len] = '\0';
    return true;
}

/* Copy a character array to the end of this StringBuffer */
bool dyn_strbuf_appendstr(StringBuffer* sb, const char* str)
{
    return dyn_strbuf_appendstrn(sb, str, strlen(str));
}

bool dyn_strbuf_appendbuff(StringBuffer* sb1, const StringBuffer* sb2)
{
    return dyn_strbuf_appendstrn(sb1, sb2->data, sb2->length);
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
 *   dyn_strutil_numofdigits(0)   = 1
 *   dyn_strutil_numofdigits(1)   = 1
 *   dyn_strutil_numofdigits(10)  = 2
 *   dyn_strutil_numofdigits(123) = 3
 */
size_t dyn_strutil_numofdigits(unsigned long v)
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
    return 12 + dyn_strutil_numofdigits(v / DYN_STRCONST_P12);
}


/* Convert integers to string to append */
bool dyn_strbuf_appendnumulong(StringBuffer* buf, unsigned long value)
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
    numdigits = dyn_strutil_numofdigits(value);
    pos = numdigits - 1;
    dyn_strbuf_ensurecapacity(buf, buf->length + numdigits);
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

bool dyn_strbuf_appendnumlong(StringBuffer* buf, long value)
{
    /* dyn_strbuf_appendformat(buf, "%li", value); */
    if(value < 0)
    {
        dyn_strbuf_appendchar(buf, '-');
        value = -value;
    }
    return dyn_strbuf_appendnumulong(buf, value);
}


bool dyn_strbuf_appendnumint(StringBuffer* buf, int value)
{
    /* dyn_strbuf_appendformat(buf, "%i", value); */
    return dyn_strbuf_appendnumlong(buf, value);
}


/* Append string converted to lowercase */
bool dyn_strbuf_appendstrnlowercase(StringBuffer* buf, const char* str, size_t len)
{
    char* to;
    const char* plength;
    dyn_strbuf_ensurecapacity(buf, buf->length + len);
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
bool dyn_strbuf_appendstrnuppercase(StringBuffer* buf, const char* str, size_t len)
{
    char* to;
    const char* end;
    dyn_strbuf_ensurecapacity(buf, buf->length + len);
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
bool dyn_strbuf_appendcharn(StringBuffer* buf, char c, size_t n)
{
    dyn_strbuf_ensurecapacity(buf, buf->length + n);
    memset(buf->data + buf->length, c, n);
    buf->length += n;
    buf->data[buf->length] = '\0';
    return true;
}

void dyn_strbuf_shrink(StringBuffer* sb, size_t len)
{
    sb->data[sb->length = (len)] = 0;
}

/*
// Remove \r and \n characters from the end of this StringBuffesr
// Returns the number of characters removed
*/
size_t dyn_strbuf_chomp(StringBuffer* sbuf)
{
    size_t oldlen;
    oldlen = sbuf->length;
    sbuf->length = dyn_strutil_chomp(sbuf->data, sbuf->length);
    return oldlen - sbuf->length;
}

/* Reverse a string */
void dyn_strbuf_reverse(StringBuffer* sbuf)
{
    dyn_strutil_reverseregion(sbuf->data, sbuf->length);
}

/*
// Get a substring as a new null terminated char array
// (remember to free the returned char* after you're done with it!)
*/
char* dyn_strbuf_substr(const StringBuffer* sbuf, size_t start, size_t len)
{
    char* newstr;
    dyn_strbuf_boundscheckreadrange(sbuf, start, len);
    newstr = (char*)nn_memory_malloc((len + 1) * sizeof(char));
    strncpy(newstr, sbuf->data + start, len);
    newstr[len] = '\0';
    return newstr;
}

void dyn_strbuf_touppercase(StringBuffer* sbuf)
{
    char* pos;
    char* end;
    end = sbuf->data + sbuf->length;
    for(pos = sbuf->data; pos < end; pos++)
    {
        *pos = (char)toupper(*pos);
    }
}

void dyn_strbuf_tolowercase(StringBuffer* sbuf)
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
// Copy a string to this StringBuffer, overwriting any existing characters
// Note: dstpos + len can be longer the the current dst StringBuffer
*/
void dyn_strbuf_copyover(StringBuffer* dst, size_t dstpos, const char* src, size_t len)
{
    size_t newlen;
    if(src == NULL || len == 0)
    {
        return;
    }
    dyn_strbuf_boundscheckinsert(dst, dstpos);
    /*
    // Check if dst buffer can handle string
    // src may have pointed to dst, which has now moved
    */
    newlen = STRBUF_MAX(dstpos + len, dst->length);
    dyn_strbuf_ensurecapacityupdateptr(dst, newlen, &src);
    /* memmove instead of strncpy, as it can handle overlapping regions */
    memmove(dst->data + dstpos, src, len * sizeof(char));
    if(dstpos + len > dst->length)
    {
        /* Extended string - add '\0' char */
        dst->length = dstpos + len;
        dst->data[dst->length] = '\0';
    }
}

/* Insert: copy to a StringBuffer, shifting any existing characters along */
void dyn_strbuf_insert(StringBuffer* dst, size_t dstpos, const char* src, size_t len)
{
    char* insert;
    if(src == NULL || len == 0)
    {
        return;
    }
    dyn_strbuf_boundscheckinsert(dst, dstpos);
    /*
    // Check if dst buffer has capacity for inserted string plus \0
    // src may have pointed to dst, which will be moved in realloc when
    // calling ensure capacity
    */
    dyn_strbuf_ensurecapacityupdateptr(dst, dst->length + len, &src);
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
//   dyn_strbuf_set(sbuf, "aaabbccc");
//   char *data = "xxx";
//   dyn_strbuf_overwrite(sbuf,3,2,data,strlen(data));
//   // sbuf is now "aaaxxxccc"
//   dyn_strbuf_overwrite(sbuf,3,2,"_",1);
//   // sbuf is now "aaa_ccc"
*/
void dyn_strbuf_overwrite(StringBuffer* dst, size_t dstpos, size_t dstlen, const char* src, size_t srclen)
{
    size_t len;
    size_t newlen;
    char* tgt;
    char* end;
    dyn_strbuf_boundscheckreadrange(dst, dstpos, dstlen);
    if(src == NULL)
    {
        return;
    }
    if(dstlen == srclen)
    {
        dyn_strbuf_copyover(dst, dstpos, src, srclen);
    }
    newlen = dst->length + srclen - dstlen;
    dyn_strbuf_ensurecapacityupdateptr(dst, newlen, &src);
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
//   dyn_strbuf_set(sb, "aaaBBccc");
//   dyn_strbuf_erase(sb, 3, 2);
//   // sb is now "aaaccc"
*/
void dyn_strbuf_erase(StringBuffer* sbuf, size_t pos, size_t len)
{
    dyn_strbuf_boundscheckreadrange(sbuf, pos, len);
    memmove(sbuf->data + pos, sbuf->data + pos + len, sbuf->length - pos - len);
    sbuf->length -= len;
    sbuf->data[sbuf->length] = '\0';
}

/*
// sprintf
*/

int dyn_strbuf_appendformatposv(StringBuffer* sbuf, size_t pos, const char* fmt, va_list argptr)
{
    size_t buflen;
    int numchars;
    va_list vacpy;
    dyn_strbuf_boundscheckinsert(sbuf, pos);
    /* Length of remaining buffer */
    buflen = sbuf->capacity - pos;
    if(buflen == 0 && !dyn_strbuf_resize(sbuf, sbuf->capacity << 1))
    {
        fprintf(stderr, "%s:%i:Error: Out of memory\n", __FILE__, __LINE__);
        dyn_strbuf_exitonerror();
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
        fprintf(stderr, "Warning: dyn_strbuf_appendformatv something went wrong..\n");
        dyn_strbuf_exitonerror();
    }
    /* numchars does not include the null terminating byte */
    if((size_t)numchars + 1 > buflen)
    {
        dyn_strbuf_ensurecapacity(sbuf, pos + (size_t)numchars);
        /*
        // now use the argptr copy we made earlier
        // Don't need to use vsnprintf now, vsprintf will do since we know it'll fit
        */
        numchars = vsprintf(sbuf->data + pos, fmt, vacpy);
        if(numchars < 0)
        {
            fprintf(stderr, "Warning: dyn_strbuf_appendformatv something went wrong..\n");
            dyn_strbuf_exitonerror();
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

int dyn_strbuf_appendformatv(StringBuffer* sbuf, const char* fmt, va_list argptr)
{
    return dyn_strbuf_appendformatposv(sbuf, sbuf->length, fmt, argptr);
}

/* sprintf to the end of a StringBuffer (adds string terminator after sprint) */
int dyn_strbuf_appendformat(StringBuffer* sbuf, const char* fmt, ...)
{
    int numchars;
    va_list argptr;
    va_start(argptr, fmt);
    numchars = dyn_strbuf_appendformatposv(sbuf, sbuf->length, fmt, argptr);
    va_end(argptr);
    return numchars;
}

/* Print at a given position (overwrite chars at positions >= pos) */
int dyn_strbuf_appendformatat(StringBuffer* sbuf, size_t pos, const char* fmt, ...)
{
    int numchars;
    va_list argptr;
    dyn_strbuf_boundscheckinsert(sbuf, pos);
    va_start(argptr, fmt);
    numchars = dyn_strbuf_appendformatposv(sbuf, pos, fmt, argptr);
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
int dyn_strbuf_appendformatnoterm(StringBuffer* sbuf, size_t pos, const char* fmt, ...)
{
    size_t len;
    int nchars;
    char lastchar;
    va_list argptr;
    dyn_strbuf_boundscheckinsert(sbuf, pos);
    len = sbuf->length;
    /* Call vsnprintf with NULL, 0 to get resulting string length without writing */
    va_start(argptr, fmt);
    nchars = vsnprintf(NULL, 0, fmt, argptr);
    va_end(argptr);
    if(nchars < 0)
    {
        fprintf(stderr, "Warning: dyn_strbuf_appendformatv something went wrong..\n");
        dyn_strbuf_exitonerror();
    }
    /* Save overwritten char */
    lastchar = (pos + (size_t)nchars < sbuf->length) ? sbuf->data[pos + (size_t)nchars] : 0;
    va_start(argptr, fmt);
    nchars = dyn_strbuf_appendformatposv(sbuf, pos, fmt, argptr);
    va_end(argptr);
    if(nchars < 0)
    {
        fprintf(stderr, "Warning: dyn_strbuf_appendformatv something went wrong..\n");
        dyn_strbuf_exitonerror();
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
void dyn_strbuf_triminplace(StringBuffer* sbuf)
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
void dyn_strbuf_trimleftinplace(StringBuffer* sbuf, const char* list)
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
void dyn_strbuf_trimrightinplace(StringBuffer* sbuf, const char* list)
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

