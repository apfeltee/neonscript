
#include "neon.h"

static int g_neon_ttycheck = -1;

const char* nn_util_color(NNColor tc)
{
    #if !defined(NEON_CONFIG_FORCEDISABLECOLOR)
        int fdstdout;
        int fdstderr;
        if(g_neon_ttycheck == -1)
        {
            fdstdout = fileno(stdout);
            fdstderr = fileno(stderr);
            g_neon_ttycheck = (osfn_isatty(fdstderr) && osfn_isatty(fdstdout));
        }
        if(g_neon_ttycheck)
        {
            switch(tc)
            {
                case NEON_COLOR_RESET:
                    return "\x1B[0m";
                case NEON_COLOR_RED:
                    return "\x1B[31m";
                case NEON_COLOR_GREEN:
                    return "\x1B[32m";
                case NEON_COLOR_YELLOW:
                    return "\x1B[33m";
                case NEON_COLOR_BLUE:
                    return "\x1B[34m";
                case NEON_COLOR_MAGENTA:
                    return "\x1B[35m";
                case NEON_COLOR_CYAN:
                    return "\x1B[36m";
            }
        }
    #else
        (void)tc;
    #endif
    return "";
}

char* nn_util_strndup(const char* src, size_t len)
{
    char* buf;
    buf = (char*)nn_memory_malloc(sizeof(char) * (len+1));
    if(buf == NULL)
    {
        return NULL;
    }
    memset(buf, 0, len+1);
    memcpy(buf, src, len);
    return buf;
}

char* nn_util_strdup(const char* src)
{
    return nn_util_strndup(src, strlen(src));
}

char* nn_util_filereadhandle(NNState* state, FILE* hnd, size_t* dlen, bool havemaxsz, size_t maxsize)
{
    long rawtold;
    /*
    * the value returned by ftell() may not necessarily be the same as
    * the amount that can be read.
    * since we only ever read a maximum of $toldlen, there will
    * be no memory trashing.
    */
    size_t toldlen;
    size_t actuallen;
    char* buf;
    (void)state;
    if(fseek(hnd, 0, SEEK_END) == -1)
    {
        return NULL;
    }
    if((rawtold = ftell(hnd)) == -1)
    {
        return NULL;
    }
    toldlen = rawtold;
    if(fseek(hnd, 0, SEEK_SET) == -1)
    {
        return NULL;
    }
    if(havemaxsz)
    {
        if(toldlen > maxsize)
        {
            toldlen = maxsize;
        }
    }
    buf = (char*)nn_memory_malloc(sizeof(char) * (toldlen + 1));
    memset(buf, 0, toldlen+1);
    if(buf != NULL)
    {
        actuallen = fread(buf, sizeof(char), toldlen, hnd);
        /*
        // optionally, read remainder:
        size_t tmplen;
        if(actuallen < toldlen)
        {
            tmplen = actuallen;
            actuallen += fread(buf+tmplen, sizeof(char), actuallen-toldlen, hnd);
            ...
        }
        // unlikely to be necessary, so not implemented.
        */
        if(dlen != NULL)
        {
            *dlen = actuallen;
        }
        return buf;
    }
    return NULL;
}

char* nn_util_filereadfile(NNState* state, const char* filename, size_t* dlen, bool havemaxsz, size_t maxsize)
{
    char* b;
    FILE* fh;
    fh = fopen(filename, "rb");
    if(fh == NULL)
    {
        return NULL;
    }
    #if defined(NEON_PLAT_ISWINDOWS)
        _setmode(fileno(fh), _O_BINARY);
    #endif
    b = nn_util_filereadhandle(state, fh, dlen, havemaxsz, maxsize);
    fclose(fh);
    return b;
}

char* nn_util_filegetshandle(char* s, int size, FILE *f, size_t* lendest)
{
    int c;
    char *p;
    p = s;
    (*lendest) = 0;
    if (size > 0)
    {
        while (--size > 0) 
        {
            if ((c = getc(f)) == -1)
            {
                if (ferror(f) == EINTR)
                {
                    continue;
                }
                break;
            }
            *p++ = c & 0xff;
            (*lendest) += 1;
            if(c == '\n')
            {
                break;
            }
        }
        *p = '\0';
    }
    if(p > s)
    {
        return s;
    }
    return NULL;
}


int nn_util_filegetlinehandle(char **lineptr, size_t *destlen, FILE* hnd)
{
    enum { kInitialStrBufSize = 256 };
    static char stackbuf[kInitialStrBufSize];
    char *heapbuf;
    size_t getlen;
    unsigned int linelen;
    getlen = 0;
    if(lineptr == NULL || destlen == NULL)
    {
        errno = EINVAL;
        return -1;
    }
    if(ferror(hnd))
    {
        return -1;
    }
    if (feof(hnd))
    {
        return -1;     
    }
    nn_util_filegetshandle(stackbuf,kInitialStrBufSize,hnd, &getlen);
    heapbuf = strchr(stackbuf,'\n');   
    if(heapbuf)
    {
        *heapbuf = '\0';
    }
    linelen = getlen;
    if((linelen+1) < kInitialStrBufSize)
    {
        heapbuf = (char*)nn_memory_realloc(*lineptr, kInitialStrBufSize);
        if(heapbuf == NULL)
        {
            return -1;
        }
        *lineptr = heapbuf;
        *destlen = kInitialStrBufSize;
    }
    strcpy(*lineptr,stackbuf);
    *destlen = linelen;
    return linelen;
}

char* nn_util_strtoupper(char* str, size_t length)
{
    int c;
    size_t i;
    for(i=0; i<length; i++)
    {
        c = str[i];
        str[i] = toupper(c);
    }
    return str;
}

char* nn_util_strtolower(char* str, size_t length)
{
    int c;
    size_t i;
    for(i=0; i<length; i++)
    {
        c = str[i];
        str[i] = toupper(c);
    }
    return str;
}


NNObjString* nn_util_numbertobinstring(NNState* state, long n)
{
    int i;
    int rem;
    int count;
    int length;
    long j;
    /* assume maximum of 1024 bits */
    char str[1024];
    char newstr[1027];
    count = 0;
    j = n;
    if(j == 0)
    {
        str[count++] = '0';
    }
    while(j != 0)
    {
        rem = abs((int)(j % 2));
        j /= 2;
        if(rem == 1)
        {
            str[count] = '1';
        }
        else
        {
            str[count] = '0';
        }
        count++;
    }
    /* assume maximum of 1024 bits + 0b (indicator) + sign (-). */
    length = 0;
    if(n < 0)
    {
        newstr[length++] = '-';
    }
    newstr[length++] = '0';
    newstr[length++] = 'b';
    for(i = count - 1; i >= 0; i--)
    {
        newstr[length++] = str[i];
    }
    newstr[length++] = 0;
    return nn_string_copylen(state, newstr, length);
    /*
    //  // To store the binary number
    //  int64_t number = 0;
    //  int cnt = 0;
    //  while (n != 0) {
    //    int64_t rem = n % 2;
    //    int64_t c = (int64_t) pow(10, cnt);
    //    number += rem * c;
    //    n /= 2;
    //
    //    // Count used to store exponent value
    //    cnt++;
    //  }
    //
    //  char str[67]; // assume maximum of 64 bits + 2 binary indicators (0b)
    //  int length = sprintf(str, "0b%lld", number);
    //
    //  return nn_string_copylen(state, str, length);
    */
}

NNObjString* nn_util_numbertooctstring(NNState* state, int64_t n, bool numeric)
{
    int length;
    /* assume maximum of 64 bits + 2 octal indicators (0c) */
    char str[66];
    length = sprintf(str, numeric ? "0c%lo" : "%lo", n);
    return nn_string_copylen(state, str, length);
}

NNObjString* nn_util_numbertohexstring(NNState* state, int64_t n, bool numeric)
{
    int length;
    /* assume maximum of 64 bits + 2 hex indicators (0x) */
    char str[66];
    length = sprintf(str, numeric ? "0x%lx" : "%lx", n);
    return nn_string_copylen(state, str, length);
}

uint32_t nn_object_hashobject(NNObject* object)
{
    switch(object->type)
    {
        case NEON_OBJTYPE_CLASS:
            {
                /* Classes just use their name. */
                return ((NNObjClass*)object)->name->hashvalue;
            }
            break;
        case NEON_OBJTYPE_FUNCSCRIPT:
            {
                /*
                // Allow bare (non-closure) functions so that we can use a map to find
                // existing constants in a function's constant table. This is only used
                // internally. Since user code never sees a non-closure function, they
                // cannot use them as map keys.
                */
                uint32_t tmpa;
                uint32_t tmpb;
                uint32_t tmpres;
                uint32_t tmpptr;
                NNObjFunction* fn;
                fn = (NNObjFunction*)object;
                tmpptr = (uint32_t)(uintptr_t)fn; 
                tmpa = nn_util_hashdouble(fn->fnscriptfunc.arity);
                tmpb = nn_util_hashdouble(fn->fnscriptfunc.blob.count);
                tmpres = tmpa ^ tmpb;
                tmpres = tmpres ^ tmpptr;
                return tmpres;
            }
            break;
        case NEON_OBJTYPE_STRING:
            {
                return ((NNObjString*)object)->hashvalue;
            }
            break;
        default:
            break;
    }
    return 0;
}

uint32_t nn_value_hashvalue(NNValue value)
{
    if(nn_value_isbool(value))
    {
        return nn_value_asbool(value) ? 3 : 5;
    }
    else if(nn_value_isnull(value))
    {
        return 7;
    }
    else if(nn_value_isnumber(value))
    {
        return nn_util_hashdouble(nn_value_asnumber(value));
    }
    else if(nn_value_isobject(value))
    {
        return nn_object_hashobject(nn_value_asobject(value));
    }
    return 0;
}
 