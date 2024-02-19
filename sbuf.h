
#pragma once

#define NEON_UTIL_MIN(x, y) ((x) < (y) ? (x) : (y))
#define NEON_UTIL_MAX(x, y) ((x) > (y) ? (x) : (y))
#define NEON_UTIL_STRBUFBOUNDSCHECKINSERT(sbuf, pos) sbuf->callBoundsCheckInsert(pos, __FILE__, __LINE__, __func__)
#define NEON_UTIL_STRBUFBOUNDSCHECKREADRANGE(sbuf, start, len) sbuf->callBoundsCheckReadRange(start, len, __FILE__, __LINE__, __func__)

namespace neon
{
    namespace Util
    {
        struct StrBuffer
        {
            public:
                static size_t roundUpToPowOf64(unsigned long long x)
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

                static void checkBufCapacity(char** buf, size_t* sizeptr, size_t len)
                {
                    /* for nul byte */
                    len++;
                    if(*sizeptr < len)
                    {
                        *sizeptr = roundUpToPowOf64(len);
                        /* fprintf(stderr, "sizeptr=%ld\n", *sizeptr); */
                        if((*buf = (char*)Memory::osRealloc(*buf, *sizeptr)) == NULL)
                        {
                            fprintf(stderr, "[%s:%i] Out of memory\n", __FILE__, __LINE__);
                            abort();
                        }
                    }
                }

                static bool repChHelper(char *dest, const char *srcstr, size_t srclen, int findme, const char* substr, size_t sublen, size_t maxlen, size_t* dlen)
                {
                    /* ch(ar) at pos(ition) */
                    int chatpos;
                    /* printf("'%p' '%s' %c\n", dest, srcstr, findme); */
                    if(*srcstr == findme)
                    {
                        if(sublen > maxlen)
                        {
                            return false;
                        }
                        if(!repChHelper(dest + sublen, srcstr + 1, srclen, findme, substr, sublen, maxlen - sublen, dlen))
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
                    chatpos = *srcstr;
                    if(*srcstr)
                    {
                        *dlen += 1;
                        if(!repChHelper(dest + 1, srcstr + 1, srclen, findme, substr, sublen, maxlen - 1, dlen))
                        {
                            return false;
                        }
                    }
                    *dest = chatpos;
                    return true;
                }

                static size_t replaceCharInPlace(char* target, size_t tgtlen, int findme, const char* sstr, size_t slen, size_t maxlen)
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
                    if(*sstr == 0)
                    {
                        /* Insure target does not shrink. */
                        return 0;
                    }
                    nlen = 0;
                    repChHelper(target, target, tgtlen, findme, sstr, slen, maxlen - 1, &nlen);
                    return nlen;
                }

                /* via: https://stackoverflow.com/a/32413923 */
                inline static void replaceFullInPlace(char* target, size_t tgtlen, const char *fstr, size_t flen, const char *sstr, size_t slen)
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
                        p = strstr(tmp, fstr);
                        /* walked past last occurrence of fstr; copy remaining part */
                        if (p == NULL)
                        {
                            strcpy(inspoint, tmp);
                            break;
                        }
                        /* copy part before fstr */
                        memcpy(inspoint, tmp, p - tmp);
                        inspoint += p - tmp;
                        /* copy sstr string */
                        memcpy(inspoint, sstr, slen);
                        inspoint += slen;
                        /* adjust pointers, move on */
                        tmp = p + flen;
                    }
                    /* write altered string back to target */
                    strcpy(target, buffer);
                }

                inline static size_t strReplaceCount(const char* str, size_t slen, const char* findstr, size_t findlen, size_t sublen)
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
                    return slen + 1;
                }

                /*
                // Removes \r and \n from the ends of a string and returns the new length
                */
                inline static size_t strChomp(char* str, size_t len)
                {
                    while(len > 0 && (str[len - 1] == '\r' || str[len - 1] == '\n'))
                    {
                        len--;
                    }
                    str[len] = '\0';
                    return len;
                }

                /*
                // Reverse a string region
                */
                inline static void strReverseRegion(char* str, size_t len)
                {
                    char *a;
                    char* b;
                    char tmp;
                    a = str;
                    b = str + len - 1;
                    while(a < b)
                    {
                        tmp = *a;
                        *a = *b;
                        *b = tmp;
                        a++;
                        b--;
                    }
                }

                /*
                 * Integer to string functions adapted from:
                 *   https://www.facebook.com/notes/facebook-engineering/three-optimization-tips-for-c/10151361643253920
                 */
                enum
                {
                    DYN_STRCONST_P01 = 10,
                    DYN_STRCONST_P02 = 100,
                    DYN_STRCONST_P03 = 1000,
                    DYN_STRCONST_P04 = 10000,
                    DYN_STRCONST_P05 = 100000,
                    DYN_STRCONST_P06 = 1000000,
                    DYN_STRCONST_P07 = 10000000,
                    DYN_STRCONST_P08 = 100000000,
                    DYN_STRCONST_P09 = 1000000000,
                    DYN_STRCONST_P10 = 10000000000,
                    DYN_STRCONST_P11 = 100000000000,
                    DYN_STRCONST_P12 = 1000000000000,
                };

                /**
                 * Return number of digits required to represent `num` in base 10.
                 * Uses binary search to find number.
                 * Examples:
                 *   numOfDigits(0)   = 1
                 *   numOfDigits(1)   = 1
                 *   numOfDigits(10)  = 2
                 *   numOfDigits(123) = 3
                 */
                inline static size_t numOfDigits(unsigned long v)
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
                    return 12 + numOfDigits(v / DYN_STRCONST_P12);
                }


            public:
                char* m_data = nullptr;

                /* total length of this buffer */
                uint64_t m_length = 0;

                /* capacity should be >= length+1 to allow for \0 */
                uint64_t m_capacity = 0;

            private:
                /* Convert integers to string to append */
                inline bool appendNumULong(unsigned long value)
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
                    numdigits = numOfDigits(value);
                    pos = numdigits - 1;
                    ensureCapacity(m_length + numdigits);
                    dst = m_data + m_length;
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
                    m_length += numdigits;
                    m_data[m_length] = '\0';
                    return true;
                }

                inline bool appendNumLong(long value)
                {
                    if(value < 0)
                    {
                        append('-');
                        value = -value;
                    }
                    return appendNumULong(value);
                }

                inline bool appendNumInt(int value)
                {
                    return appendNumLong(value);
                }

            public:
                inline void callBoundsCheckInsert(size_t pos, const char* file, int line, const char* func) const
                {
                    if(pos > m_length)
                    {
                        fprintf(stderr, "%s:%i:%s() - out of bounds error [index: %zu, num_of_bits: %zu]\n",
                                file, line, func, pos, m_length);
                        errno = EDOM;
                        abort();
                    }
                }

                /* Bounds check when reading a range (start+len < strlen is valid) */
                inline void callBoundsCheckReadRange(size_t start, size_t len, const char* file, int line, const char* func) const
                {
                    size_t maxlen;
                    const char* ends;
                    if(start + len > m_length)
                    {
                        maxlen = NEON_UTIL_MIN(5, m_length);
                        ends = "";
                        if(m_length > 5)
                        {
                            ends = "...";
                        }
                        fprintf(stderr,"%s:%i:%s() - out of bounds error [start: %zu; length: %zu; strlen: %zu; buf:%.*s%s]\n",
                                file, line, func, start, len, m_length, (int)maxlen, m_data, ends);
                        errno = EDOM;
                        abort();
                    }
                }

                /* Ensure capacity for len characters plus '\0' character - exits on FAILURE */
                inline void ensureCapacity(uint64_t len)
                {
                    checkBufCapacity(&m_data, &m_capacity, len);
                }

            private:
                inline void resetVars()
                {
                    m_length = 0;
                    m_capacity = 0;
                    m_data = nullptr;
                }

                inline void initEmpty(size_t len)
                {
                    resetVars();
                    m_length = len;
                    m_capacity = roundUpToPowOf64(len + 1);
                    m_data = (char*)Memory::osMalloc(m_capacity);
                    m_data[0] = '\0';
                }

            public:
                inline StrBuffer()
                {
                    initEmpty(0);
                }

                inline StrBuffer(size_t len)
                {
                    (void)len;
                    //initEmpty(len);
                    initEmpty(0);
                }

                /*
                // Copy a string or existing string buffer
                */
                inline StrBuffer(const char* str, size_t slen)
                {
                    initEmpty(slen + 1);
                    memcpy(m_data, str, slen);
                    m_length = slen;
                    m_data[slen] = '\0';
                }

                inline ~StrBuffer()
                {
                    destroy();
                }

                inline uint64_t size() const
                {
                    return m_length;
                }

                inline uint64_t length() const
                {
                    return m_length;
                }

                inline const char* data() const
                {
                    return m_data;
                }

                inline bool destroy()
                {
                    if(m_data != nullptr)
                    {
                        Memory::osFree(m_data);
                        m_data = nullptr;
                        resetVars();
                    }
                    return true;
                }

                /*
                * Clear the content of an existing StrBuffer (sets size to 0)
                * but keeps capacity intact.
                */
                void reset()
                {
                    size_t olen;
                    if(m_data != nullptr)
                    {
                        olen = m_length;
                        m_length = 0;
                        memset(m_data, 0, olen);
                        m_data[0] = '\0';
                    }
                }

                /*
                // Resize the buffer to have capacity to hold a string of m_length newlen
                // (+ a null terminating character).  Can also be used to downsize the buffer's
                // memory usage.  Returns 1 on success, 0 on failure.
                */
                bool resize(size_t newlen)
                {
                    size_t ncap;
                    char* newbuf;
                    ncap = roundUpToPowOf64(newlen + 1);
                    newbuf = (char*)Memory::osRealloc(m_data, ncap * sizeof(char));
                    if(newbuf == NULL)
                    {
                        return false;
                    }
                    m_data = newbuf;
                    m_capacity = ncap;
                    if(m_length > newlen)
                    {
                        /* Buffer was shrunk - re-add null byte */
                        m_length = newlen;
                        m_data[m_length] = '\0';
                    }
                    return true;
                }

                /* Same as above, but update pointer if it pointed to resized array */
                void ensureCapacityUpdatePtr(size_t size, const char** ptr)
                {
                    size_t oldcap;
                    char* oldbuf;
                    if(m_capacity <= size + 1)
                    {
                        oldcap = m_capacity;
                        oldbuf = m_data;
                        if(!resize(size))
                        {
                            fprintf(stderr,
                                    "%s:%i:Error: _ensure_capacity_update_ptr couldn't resize "
                                    "buffer. [requested %zu bytes; capacity: %zu bytes]\n",
                                    __FILE__, __LINE__, size, m_capacity);
                            abort();
                        }
                        /* ptr may have pointed to sbuf, which has now moved */
                        if(*ptr >= oldbuf && *ptr < oldbuf + oldcap)
                        {
                            *ptr = m_data + (*ptr - oldbuf);
                        }
                    }
                }

                /*
                // Copy N characters from a character array to the end of this StrBuffer
                // strlen(str) must be >= len
                */
                bool append(const char* str, size_t len)
                {
                    ensureCapacityUpdatePtr(m_length + len, &str);
                    memcpy(m_data + m_length, str, len);
                    m_data[m_length = m_length + len] = '\0';
                    return true;
                }

                /* Copy a character array to the end of this StrBuffer */
                bool append(const char* str)
                {
                    return append(str, strlen(str));
                }

                bool append(const StrBuffer* sb2)
                {
                    return append(sb2->m_data, sb2->m_length);
                }

                /* Add a character to the end of this StrBuffer */
                bool append(int c)
                {
                    this->ensureCapacity(m_length + 1);
                    m_data[m_length] = c;
                    m_data[++m_length] = '\0';
                    return true;
                }

                /* Append char `c` `n` times */
                bool appendCharN(char c, size_t n)
                {
                    ensureCapacity(m_length + n);
                    memset(m_data + m_length, c, n);
                    m_length += n;
                    m_data[m_length] = '\0';
                    return true;
                }

                /*
                // sprintf
                */
                int appendFormatv(size_t pos, const char* fmt, va_list argptr)
                {
                    size_t buflen;
                    int numchars;
                    va_list vacpy;
                    NEON_UTIL_STRBUFBOUNDSCHECKINSERT(this, pos);
                    /* Length of remaining buffer */
                    buflen = m_capacity - pos;
                    if(buflen == 0 && !this->resize(m_capacity << 1))
                    {
                        fprintf(stderr, "%s:%i:Error: Out of memory\n", __FILE__, __LINE__);
                        abort();
                    }
                    /* Make a copy of the list of args incase we need to resize buff and try again */
                    va_copy(vacpy, argptr);
                    numchars = vsnprintf(m_data + pos, buflen, fmt, argptr);
                    va_end(argptr);
                    /*
                    // numchars is the number of chars that would be written (not including '\0')
                    // numchars < 0 => failure
                    */
                    if(numchars < 0)
                    {
                        fprintf(stderr, "Warning: appendFormatv something went wrong..\n");
                        abort();
                    }
                    /* numchars does not include the null terminating byte */
                    if((size_t)numchars + 1 > buflen)
                    {
                        this->ensureCapacity(pos + (size_t)numchars);
                        /*
                        // now use the argptr copy we made earlier
                        // Don't need to use vsnprintf now, vsprintf will do since we know it'll fit
                        */
                        numchars = vsprintf(m_data + pos, fmt, vacpy);
                        if(numchars < 0)
                        {
                            fprintf(stderr, "Warning: appendFormatv something went wrong..\n");
                            abort();
                        }
                    }
                    va_end(vacpy);
                    /*
                    // Don't need to NUL terminate, vsprintf/vnsprintf does that for us
                    // Update m_length
                    */
                    m_length = pos + (size_t)numchars;
                    return numchars;
                }

                /* sprintf to the end of a StrBuffer (adds string terminator after sprint) */
                int appendFormat(const char* fmt, ...)
                {
                    int numchars;
                    va_list argptr;
                    va_start(argptr, fmt);
                    numchars = this->appendFormatv(m_length, fmt, argptr);
                    va_end(argptr);
                    return numchars;
                }

                /* Print at a given position (overwrite chars at positions >= pos) */
                int appendFormatAt(size_t pos, const char* fmt, ...)
                {
                    int numchars;
                    va_list argptr;
                    NEON_UTIL_STRBUFBOUNDSCHECKINSERT(this, pos);
                    va_start(argptr, fmt);
                    numchars = this->appendFormatv(pos, fmt, argptr);
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
                int appendFormatNoTerm(size_t pos, const char* fmt, ...)
                {
                    size_t len;
                    int nchars;
                    char lastchar;
                    NEON_UTIL_STRBUFBOUNDSCHECKINSERT(this, pos);
                    len = m_length;
                    /* Call vsnprintf with NULL, 0 to get resulting string m_length without writing */
                    va_list argptr;
                    va_start(argptr, fmt);
                    nchars = vsnprintf(NULL, 0, fmt, argptr);
                    va_end(argptr);
                    if(nchars < 0)
                    {
                        fprintf(stderr, "Warning: appendFormatNoTerm something went wrong..\n");
                        abort();
                    }
                    /* Save overwritten char */
                    lastchar = (pos + (size_t)nchars < m_length) ? m_data[pos + (size_t)nchars] : 0;
                    va_start(argptr, fmt);
                    nchars = this->appendFormatv(pos, fmt, argptr);
                    va_end(argptr);
                    if(nchars < 0)
                    {
                        fprintf(stderr, "Warning: appendFormatNoTerm something went wrong..\n");
                        abort();
                    }
                    /* Restore m_length if shrunk, null terminate if extended */
                    if(m_length < len)
                    {
                        m_length = len;
                    }
                    else
                    {
                        m_data[m_length] = '\0';
                    }
                    /* Re-instate overwritten character */
                    m_data[pos + (size_t)nchars] = lastchar;
                    return nchars;
                }

                bool contains(char ch)
                {
                    size_t i;
                    for(i=0; i<m_length; i++)
                    {
                        if(m_data[i] == ch)
                        {
                            return true;
                        }
                    }
                    return false;
                }

                bool replace(int findme, const char* sstr, size_t slen)
                {
                    size_t i;
                    size_t nlen;
                    size_t needed;
                    needed = m_capacity;
                    for(i=0; i<m_length; i++)
                    {
                        if(m_data[i] == findme)
                        {
                            needed += slen;
                        }
                    }
                    if(!this->resize(needed+1))
                    {
                        return false;
                    }
                    nlen = replaceCharInPlace(m_data, m_length, findme, sstr, slen, m_capacity);
                    m_length = nlen;
                    return true;
                }

                bool fullReplace(const char* fstr, size_t flen, const char* sstr, size_t slen)
                {
                    size_t needed;
                    StrBuffer* nbuf;
                    needed = strReplaceCount(m_data, m_length, fstr, flen, slen);
                    if(needed == 0)
                    {
                        return false;
                    }
                    nbuf = Memory::create<StrBuffer>(needed);
                    nbuf->append(m_data, m_length);
                    StrBuffer::replaceFullInPlace(nbuf->m_data, nbuf->m_length, fstr, flen, sstr, slen);
                    nbuf->m_length = needed;
                    Memory::osFree(m_data);
                    m_data = nbuf->m_data;
                    m_length = nbuf->m_length;
                    m_capacity = nbuf->m_capacity;
                    return true;
                }

                /*
                // Copy a string to this StrBuffer, overwriting any existing characters
                // Note: dstpos + len can be longer the the current dst StrBuffer
                */
                void copyOver(size_t dstpos, const char* srcstr, size_t srclen)
                {
                    size_t newlen;
                    if(srcstr == NULL || srclen == 0)
                    {
                        return;
                    }
                    NEON_UTIL_STRBUFBOUNDSCHECKINSERT(this, dstpos);
                    /*
                    // Check if dst buffer can handle string
                    // srcstr may have pointed to dst, which has now moved
                    */
                    newlen = NEON_UTIL_MAX(dstpos + srclen, m_length);
                    ensureCapacityUpdatePtr(newlen, &srcstr);
                    /* memmove instead of strncpy, as it can handle overlapping regions */
                    memmove(m_data + dstpos, srcstr, srclen * sizeof(char));
                    if(dstpos + srclen > m_length)
                    {
                        /* Extended string - add '\0' char */
                        m_length = dstpos + srclen;
                        m_data[m_length] = '\0';
                    }
                }

                /*
                // Overwrite dstpos..(dstpos+dstlen-1) with srclen chars from srcstr
                // if dstlen != srclen, content to the right of dstlen is shifted
                // Example:
                //   sbuf = ... "aaabbccc";
                //   char *data = "xxx";
                //   sbuf->replaceAt(3,2,data,strlen(data));
                //   // sbuf is now "aaaxxxccc"
                //   sbuf->replaceAt(3,2,"_",1);
                //   // sbuf is now "aaa_ccc"
                */
                void replaceAt(size_t dstpos, size_t dstlen, const char* srcstr, size_t srclen)
                {
                    size_t len;
                    size_t newlen;
                    char* tgt;
                    char* end;
                    NEON_UTIL_STRBUFBOUNDSCHECKREADRANGE(this, dstpos, dstlen);
                    if(srcstr == NULL)
                    {
                        return;
                    }
                    if(dstlen == srclen)
                    {
                        this->copyOver(dstpos, srcstr, srclen);
                    }
                    newlen = m_length + srclen - dstlen;
                    ensureCapacityUpdatePtr(newlen, &srcstr);
                    if(srcstr >= m_data && srcstr < m_data + m_capacity)
                    {
                        if(srclen < dstlen)
                        {
                            /* copy */
                            memmove(m_data + dstpos, srcstr, srclen * sizeof(char));
                            /* resize (shrink) */
                            memmove(m_data + dstpos + srclen, m_data + dstpos + dstlen, (m_length - dstpos - dstlen) * sizeof(char));
                        }
                        else
                        {
                            /*
                            // Buffer is going to grow and srcstr points to this buffer
                            // resize (grow)
                            */
                            memmove(m_data + dstpos + srclen, m_data + dstpos + dstlen, (m_length - dstpos - dstlen) * sizeof(char));
                            tgt = m_data + dstpos;
                            end = m_data + dstpos + srclen;
                            if(srcstr < tgt + dstlen)
                            {
                                len = NEON_UTIL_MIN((size_t)(end - srcstr), srclen);
                                memmove(tgt, srcstr, len);
                                tgt += len;
                                srcstr += len;
                                srclen -= len;
                            }
                            if(srcstr >= tgt + dstlen)
                            {
                                /* shift to account for resizing */
                                srcstr += srclen - dstlen;
                                memmove(tgt, srcstr, srclen);
                            }
                        }
                    }
                    else
                    {
                        /* resize */
                        memmove(m_data + dstpos + srclen, m_data + dstpos + dstlen, (m_length - dstpos - dstlen) * sizeof(char));
                        /* copy */
                        memcpy(m_data + dstpos, srcstr, srclen * sizeof(char));
                    }
                    m_length = newlen;
                    m_data[m_length] = '\0';
                }

                void shrinkk(size_t len)
                {
                    m_data[m_length = (len)] = 0;
                }

                /*
                // Remove \r and \n characters from the end of this StringBuffesr
                // Returns the number of characters removed
                */
                size_t chomp()
                {
                    size_t oldlen;
                    oldlen = m_length;
                    m_length = strChomp(m_data, m_length);
                    return oldlen - m_length;
                }


                /* Reverse a string */
                void reverse()
                {
                    strReverseRegion(m_data, m_length);
                }

                /*
                // Get a substring as a new null terminated char array
                // (remember to free the returned char* after you're done with it!)
                */
                char* substr(size_t start, size_t len)
                {
                    char* newstr;
                    NEON_UTIL_STRBUFBOUNDSCHECKREADRANGE(this, start, len);
                    newstr = (char*)Memory::osMalloc((len + 1) * sizeof(char));
                    strncpy(newstr, m_data + start, len);
                    newstr[len] = '\0';
                    return newstr;
                }

                void toUpperCase()
                {
                    char* pos;
                    char* end;
                    end = m_data + m_length;
                    for(pos = m_data; pos < end; pos++)
                    {
                        *pos = (char)toupper(*pos);
                    }
                }

                void toLowerCase()
                {
                    char* pos;
                    char* end;
                    end = m_data + m_length;
                    for(pos = m_data; pos < end; pos++)
                    {
                        *pos = (char)tolower(*pos);
                    }
                }

                /* Insert: copy to a StrBuffer, shifting any existing characters along */
                void insertAt(StrBuffer* dst, size_t dstpos, const char* srcstr, size_t srclen)
                {
                    char* insert;
                    if(srcstr == NULL || srclen == 0)
                    {
                        return;
                    }
                    NEON_UTIL_STRBUFBOUNDSCHECKINSERT(dst, dstpos);
                    /*
                    // Check if dst buffer has capacity for inserted string plus \0
                    // srcstr may have pointed to dst, which will be moved in realloc when
                    // calling ensure capacity
                    */
                    dst->ensureCapacityUpdatePtr(dst->m_length + srclen, &srcstr);
                    insert = dst->m_data + dstpos;
                    /* dstpos could be at the end (== dst->m_length) */
                    if(dstpos < dst->m_length)
                    {
                        /* Shift some characters up */
                        memmove(insert + srclen, insert, (dst->m_length - dstpos) * sizeof(char));
                        if(srcstr >= dst->m_data && srcstr < dst->m_data + dst->m_capacity)
                        {
                            /* srcstr/dst strings point to the same string in memory */
                            if(srcstr < insert)
                            {
                                memmove(insert, srcstr, srclen * sizeof(char));
                            }
                            else if(srcstr > insert)
                            {
                                memmove(insert, srcstr + srclen, srclen * sizeof(char));
                            }
                        }
                        else
                        {
                            memmove(insert, srcstr, srclen * sizeof(char));
                        }
                    }
                    else
                    {
                        memmove(insert, srcstr, srclen * sizeof(char));
                    }
                    /* Update size */
                    dst->m_length += srclen;
                    dst->m_data[dst->m_length] = '\0';
                }

                /*
                // Remove characters from the buffer
                //   sb = ... "aaaBBccc";
                //   sb->eraseAt(3, 2);
                //   // sb is now "aaaccc"
                */
                void eraseAt(size_t pos, size_t len)
                {
                    NEON_UTIL_STRBUFBOUNDSCHECKREADRANGE(this, pos, len);
                    memmove(m_data + pos, m_data + pos + len, m_length - pos - len);
                    m_length -= len;
                    m_data[m_length] = '\0';
                }

                /* Trim whitespace characters from the start and end of a string */
                void trimInplace()
                {
                    size_t start;
                    if(m_length == 0)
                    {
                        return;
                    }
                    /* Trim end first */
                    while(m_length > 0 && isspace((int)m_data[m_length - 1]))
                    {
                        m_length--;
                    }
                    m_data[m_length] = '\0';
                    if(m_length == 0)
                    {
                        return;
                    }
                    start = 0;
                    while(start < m_length && isspace((int)m_data[start]))
                    {
                        start++;
                    }
                    if(start != 0)
                    {
                        m_length -= start;
                        memmove(m_data, m_data + start, m_length * sizeof(char));
                        m_data[m_length] = '\0';
                    }
                }

                /*
                // Trim the characters listed in `list` from the left of `sbuf`
                // `list` is a null-terminated string of characters
                */
                void trimInplaceLeft(const char* list)
                {
                    size_t start;
                    start = 0;

                    while(start < m_length && (strchr(list, m_data[start]) != NULL))
                    {
                        start++;
                    }
                    if(start != 0)
                    {
                        m_length -= start;
                        memmove(m_data, m_data + start, m_length * sizeof(char));
                        m_data[m_length] = '\0';
                    }
                }

                /*
                // Trim the characters listed in `list` from the right of `sbuf`
                // `list` is a null-terminated string of characters
                */
                void trimInplaceRight(const char* list)
                {
                    if(m_length == 0)
                    {
                        return;
                    }
                    while(m_length > 0 && strchr(list, m_data[m_length - 1]) != NULL)
                    {
                        m_length--;
                    }
                    m_data[m_length] = '\0';
                }
        };
    }
}

