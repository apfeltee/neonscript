
#pragma once

/*
 stream_buffer.h
 project: string_buffer
 url: https://github.com/noporpoise/StringBuffer
 author: Isaac Turner <turner.isaac@gmail.com>
 license: Public Domain
 Jan 2015
*/

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <signal.h>
#include <ctype.h>


#if defined(__GNUC__)
    #define DYN_STRBUF_ATTRIBUTE(x) __attribute__(x)
#else
    #define DYN_STRBUF_ATTRIBUTE(x)
#endif


#if defined(__STRICT_ANSI__) && !defined(__cplusplus)
    #define va_copy(...)
#endif

#define STRBUF_MIN(x, y) ((x) < (y) ? (x) : (y))
#define STRBUF_MAX(x, y) ((x) > (y) ? (x) : (y))

#define dyn_strbuf_exitonerror()     \
    do                      \
    {                       \
        abort();            \
        exit(EXIT_FAILURE); \
    } while(0)


/* Bounds check when inserting (pos <= len are valid) */
#define dyn_strbuf_boundscheckinsert(sbuf, pos) sbuf->callBoundsCheckInsert(pos, __FILE__, __LINE__, __func__)
#define dyn_strbuf_boundscheckreadrange(sbuf, start, len) sbuf->callBoundsCheckReadRange(start, len, __FILE__, __LINE__, __func__)


#ifndef ROUNDUP2POW
    #define ROUNDUP2POW(x) dyn_strutil_rndup2pow64(x)

static size_t dyn_strutil_rndup2pow64(unsigned long long x)
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
#endif

void dyn_strutil_cbufcapacity(char** buf, size_t* sizeptr, size_t len)
{
    /* for nul byte */
    len++;
    if(*sizeptr < len)
    {
        *sizeptr = ROUNDUP2POW(len);
        /* fprintf(stderr, "sizeptr=%ld\n", *sizeptr); */
        if((*buf = (char*)realloc(*buf, *sizeptr)) == NULL)
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

struct StrBuffer
{
    public:
        /* via: https://stackoverflow.com/a/32413923 */
        static void replaceInPlace(char* target, size_t tgtlen, const char *findstr, size_t findlen, const char *substr, size_t sublen)
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

        static size_t strReplaceCount(const char* str, size_t slen, const char* findstr, size_t findlen, size_t sublen)
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
        static size_t numOfDigits(unsigned long v)
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
        bool appendNumULong(unsigned long value)
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

        bool appendNumLong(long value)
        {
            /* dyn_strbuf_appendformat(buf, "%li", value); */
            if(value < 0)
            {
                append('-');
                value = -value;
            }
            return appendNumULong(value);
        }

        bool appendNumInt(int value)
        {
            /* dyn_strbuf_appendformat(buf, "%i", value); */
            return appendNumLong(value);
        }

    public:
        void callBoundsCheckInsert(size_t pos, const char* file, int line, const char* func) const
        {
            if(pos > this->m_length)
            {
                fprintf(stderr, "%s:%i:%s() - out of bounds error [index: %zu, num_of_bits: %zu]\n",
                        file, line, func, pos, this->m_length);
                errno = EDOM;
                dyn_strbuf_exitonerror();
            }
        }

        /* Bounds check when reading a range (start+len < strlen is valid) */
        void callBoundsCheckReadRange(size_t start, size_t len, const char* file, int line, const char* func) const
        {
            if(start + len > this->m_length)
            {
                fprintf(stderr,"%s:%i:%s() - out of bounds error [start: %zu; length: %zu; strlen: %zu; buf:%.*s%s]\n",
                        file, line, func, start, len, this->m_length, (int)STRBUF_MIN(5, this->m_length), this->m_data, this->m_length > 5 ? "..." : "");
                errno = EDOM;
                dyn_strbuf_exitonerror();
            }
        }

        /* Ensure capacity for len characters plus '\0' character - exits on FAILURE */
        void ensureCapacity(uint64_t len)
        {
            dyn_strutil_cbufcapacity(&this->m_data, &this->m_capacity, len);
        }

    private:
        void resetVars()
        {
            this->m_length = 0;
            this->m_capacity = 0;
            this->m_data = nullptr;
        }

        void initEmpty(size_t len)
        {
            resetVars();
            this->m_length = len;
            this->m_capacity = ROUNDUP2POW(len + 1);
            this->m_data = (char*)malloc(this->m_capacity);
            this->m_data[0] = '\0';
        }


    public:
        StrBuffer()
        {
            initEmpty(0);
        }

        StrBuffer(size_t len)
        {
            (void)len;
            //initEmpty(len);
            initEmpty(0);
        }

        /*
        // Copy a string or existing string buffer
        */
        StrBuffer(const char* str, size_t slen)
        {
            initEmpty(slen + 1);
            memcpy(this->m_data, str, slen);
            this->m_length = slen;
            this->m_data[slen] = '\0';
        }

        ~StrBuffer()
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

        bool destroy()
        {
            if(this->m_data != nullptr)
            {
                free(this->m_data);
                this->m_data = nullptr;
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
            if(this->m_data != nullptr)
            {
                olen = this->m_length;
                this->m_length = 0;
                memset(this->m_data, 0, olen);
                this->m_data[0] = '\0';
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
            ncap = ROUNDUP2POW(newlen + 1);
            newbuf = (char*)realloc(this->m_data, ncap * sizeof(char));
            if(newbuf == NULL)
            {
                return false;
            }
            this->m_data = newbuf;
            this->m_capacity = ncap;
            if(this->m_length > newlen)
            {
                /* Buffer was shrunk - re-add null byte */
                this->m_length = newlen;
                this->m_data[this->m_length] = '\0';
            }
            return true;
        }

        /* Same as above, but update pointer if it pointed to resized array */
        void ensureCapacityUpdatePtr(size_t size, const char** ptr)
        {
            size_t oldcap;
            char* oldbuf;
            if(this->m_capacity <= size + 1)
            {
                oldcap = this->m_capacity;
                oldbuf = this->m_data;
                if(!resize(size))
                {
                    fprintf(stderr,
                            "%s:%i:Error: _ensure_capacity_update_ptr couldn't resize "
                            "buffer. [requested %zu bytes; capacity: %zu bytes]\n",
                            __FILE__, __LINE__, size, this->m_capacity);
                    dyn_strbuf_exitonerror();
                }
                /* ptr may have pointed to sbuf, which has now moved */
                if(*ptr >= oldbuf && *ptr < oldbuf + oldcap)
                {
                    *ptr = this->m_data + (*ptr - oldbuf);
                }
            }
        }

        /*
        // Copy N characters from a character array to the end of this StrBuffer
        // strlen(str) must be >= len
        */
        bool append(const char* str, size_t len)
        {
            ensureCapacityUpdatePtr(this->m_length + len, &str);
            memcpy(this->m_data + this->m_length, str, len);
            this->m_data[this->m_length = this->m_length + len] = '\0';
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
            this->ensureCapacity(this->m_length + 1);
            this->m_data[this->m_length] = c;
            this->m_data[++this->m_length] = '\0';
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
            dyn_strbuf_boundscheckinsert(this, pos);
            /* Length of remaining buffer */
            buflen = this->m_capacity - pos;
            if(buflen == 0 && !this->resize(this->m_capacity << 1))
            {
                fprintf(stderr, "%s:%i:Error: Out of memory\n", __FILE__, __LINE__);
                dyn_strbuf_exitonerror();
            }
            /* Make a copy of the list of args incase we need to resize buff and try again */
            va_copy(vacpy, argptr);
            numchars = vsnprintf(this->m_data + pos, buflen, fmt, argptr);
            va_end(argptr);
            /*
            // numchars is the number of chars that would be written (not including '\0')
            // numchars < 0 => failure
            */
            if(numchars < 0)
            {
                fprintf(stderr, "Warning: appendFormatv something went wrong..\n");
                dyn_strbuf_exitonerror();
            }
            /* numchars does not include the null terminating byte */
            if((size_t)numchars + 1 > buflen)
            {
                this->ensureCapacity(pos + (size_t)numchars);
                /*
                // now use the argptr copy we made earlier
                // Don't need to use vsnprintf now, vsprintf will do since we know it'll fit
                */
                numchars = vsprintf(this->m_data + pos, fmt, vacpy);
                if(numchars < 0)
                {
                    fprintf(stderr, "Warning: appendFormatv something went wrong..\n");
                    dyn_strbuf_exitonerror();
                }
            }
            va_end(vacpy);
            /*
            // Don't need to NUL terminate, vsprintf/vnsprintf does that for us
            // Update m_length
            */
            this->m_length = pos + (size_t)numchars;
            return numchars;
        }

        /* sprintf to the end of a StrBuffer (adds string terminator after sprint) */
        int appendFormat(const char* fmt, ...)
        {
            int numchars;
            va_list argptr;
            va_start(argptr, fmt);
            numchars = this->appendFormatv(this->m_length, fmt, argptr);
            va_end(argptr);
            return numchars;
        }

        /* Print at a given position (overwrite chars at positions >= pos) */
        int appendFormatAt(size_t pos, const char* fmt, ...)
        {
            int numchars;
            va_list argptr;
            dyn_strbuf_boundscheckinsert(this, pos);
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
            dyn_strbuf_boundscheckinsert(this, pos);
            len = this->m_length;
            /* Call vsnprintf with NULL, 0 to get resulting string m_length without writing */
            va_list argptr;
            va_start(argptr, fmt);
            nchars = vsnprintf(NULL, 0, fmt, argptr);
            va_end(argptr);
            if(nchars < 0)
            {
                fprintf(stderr, "Warning: appendFormatNoTerm something went wrong..\n");
                dyn_strbuf_exitonerror();
            }
            /* Save overwritten char */
            lastchar = (pos + (size_t)nchars < this->m_length) ? this->m_data[pos + (size_t)nchars] : 0;
            va_start(argptr, fmt);
            nchars = this->appendFormatv(pos, fmt, argptr);
            va_end(argptr);
            if(nchars < 0)
            {
                fprintf(stderr, "Warning: appendFormatNoTerm something went wrong..\n");
                dyn_strbuf_exitonerror();
            }
            /* Restore m_length if shrunk, null terminate if extended */
            if(this->m_length < len)
            {
                this->m_length = len;
            }
            else
            {
                this->m_data[this->m_length] = '\0';
            }
            /* Re-instate overwritten character */
            this->m_data[pos + (size_t)nchars] = lastchar;
            return nchars;
        }

        bool contains(char ch)
        {
            size_t i;
            for(i=0; i<this->m_length; i++)
            {
                if(this->m_data[i] == ch)
                {
                    return true;
                }
            }
            return false;
        }

        bool replace(int findme, const char* substr, size_t sublen)
        {
            size_t i;
            size_t nlen;
            size_t needed;
            needed = this->m_capacity;
            for(i=0; i<this->m_length; i++)
            {
                if(this->m_data[i] == findme)
                {
                    needed += sublen;
                }
            }
            if(!this->resize(needed+1))
            {
                return false;
            }
            nlen = dyn_strutil_inpreplace(this->m_data, this->m_length, findme, substr, sublen, this->m_capacity);
            this->m_length = nlen;
            return true;
        }

        bool fullReplace(const char* findstr, size_t findlen, const char* substr, size_t sublen)
        {
            size_t needed;
            StrBuffer* nbuf;
            needed = strReplaceCount(m_data, m_length, findstr, findlen, sublen);
            if(needed == 0)
            {
                return false;
            }
            nbuf = new StrBuffer(needed);
            nbuf->append(m_data, m_length);
            StrBuffer::replaceInPlace(nbuf->m_data, nbuf->m_length, findstr, findlen, substr, sublen);
            nbuf->m_length = needed;
            free(m_data);
            m_data = nbuf->m_data;
            m_length = nbuf->m_length;
            m_capacity = nbuf->m_capacity;
            return true;
        }

        /*
        // Copy a string to this StrBuffer, overwriting any existing characters
        // Note: dstpos + len can be longer the the current dst StrBuffer
        */
        void copyOver(size_t dstpos, const char* src, size_t len)
        {
            size_t newlen;
            if(src == NULL || len == 0)
            {
                return;
            }
            dyn_strbuf_boundscheckinsert(this, dstpos);
            /*
            // Check if dst buffer can handle string
            // src may have pointed to dst, which has now moved
            */
            newlen = STRBUF_MAX(dstpos + len, m_length);
            ensureCapacityUpdatePtr(newlen, &src);
            /* memmove instead of strncpy, as it can handle overlapping regions */
            memmove(m_data + dstpos, src, len * sizeof(char));
            if(dstpos + len > m_length)
            {
                /* Extended string - add '\0' char */
                m_length = dstpos + len;
                m_data[m_length] = '\0';
            }
        }


        /*
        // Overwrite dstpos..(dstpos+dstlen-1) with srclen chars from src
        // if dstlen != srclen, content to the right of dstlen is shifted
        // Example:
        //   sbuf = ... "aaabbccc";
        //   char *data = "xxx";
        //   sbuf->replaceAt(3,2,data,strlen(data));
        //   // sbuf is now "aaaxxxccc"
        //   sbuf->replaceAt(3,2,"_",1);
        //   // sbuf is now "aaa_ccc"
        */
        void replaceAt(size_t dstpos, size_t dstlen, const char* src, size_t srclen)
        {
            size_t len;
            size_t newlen;
            char* tgt;
            char* end;
            dyn_strbuf_boundscheckreadrange(this, dstpos, dstlen);
            if(src == NULL)
            {
                return;
            }
            if(dstlen == srclen)
            {
                this->copyOver(dstpos, src, srclen);
            }
            newlen = m_length + srclen - dstlen;
            ensureCapacityUpdatePtr(newlen, &src);
            if(src >= m_data && src < m_data + m_capacity)
            {
                if(srclen < dstlen)
                {
                    /* copy */
                    memmove(m_data + dstpos, src, srclen * sizeof(char));
                    /* resize (shrink) */
                    memmove(m_data + dstpos + srclen, m_data + dstpos + dstlen, (m_length - dstpos - dstlen) * sizeof(char));
                }
                else
                {
                    /*
                    // Buffer is going to grow and src points to this buffer
                    // resize (grow)
                    */
                    memmove(m_data + dstpos + srclen, m_data + dstpos + dstlen, (m_length - dstpos - dstlen) * sizeof(char));
                    tgt = m_data + dstpos;
                    end = m_data + dstpos + srclen;
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
                memmove(m_data + dstpos + srclen, m_data + dstpos + dstlen, (m_length - dstpos - dstlen) * sizeof(char));
                /* copy */
                memcpy(m_data + dstpos, src, srclen * sizeof(char));
            }
            m_length = newlen;
            m_data[m_length] = '\0';
        }


};


/*
********************
*  Bounds checking
********************
*/


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
            arr = (char**)malloc(txtlen * sizeof(char*));
            for(i = 0; i < txtlen; i++)
            {
                arr[i] = (char*)malloc(2 * sizeof(char));
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
    arr = (char**)malloc(count * sizeof(char*));
    count = 0;
    plastpos = sourcetxt;
    while((find = strstr(plastpos, splitat)) != NULL)
    {
        slen = (size_t)(find - plastpos);
        arr[count] = (char*)malloc((slen + 1) * sizeof(char));
        strncpy(arr[count], plastpos, slen);
        arr[count][slen] = '\0';
        count++;
        plastpos = find + splitlen;
    }
    /* Copy last item */
    slen = (size_t)(sourcetxt + txtlen - plastpos);
    arr[count] = (char*)malloc((slen + 1) * sizeof(char));
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

void dyn_strutil_strreplace1(char **str, size_t selflen, const char* findstr, size_t findlen, const char *substr, size_t sublen)
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
    buff = (char*)calloc((i + oldcount * (sublen - findlen) + 1), sizeof(char));
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
    free(*str);
    *str = (char*)calloc(i + 1, sizeof(char));
    if (!(*str))
    {
        perror("bad allocation\n");
        exit(EXIT_FAILURE);
    }
    i = 0;
    dyn_strutil_faststrncat(*str, (const char *)buff, &i);
    free(buff);
}


void dyn_strbuf_shrink(StrBuffer* sb, size_t len)
{
    sb->m_data[sb->m_length = (len)] = 0;
}

/*
// Remove \r and \n characters from the end of this StringBuffesr
// Returns the number of characters removed
*/
size_t dyn_strbuf_chomp(StrBuffer* sbuf)
{
    size_t oldlen;
    oldlen = sbuf->m_length;
    sbuf->m_length = dyn_strutil_chomp(sbuf->m_data, sbuf->m_length);
    return oldlen - sbuf->m_length;
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

/* Reverse a string */
void dyn_strbuf_reverse(StrBuffer* sbuf)
{
    dyn_strutil_reverseregion(sbuf->m_data, sbuf->m_length);
}

/*
// Get a substring as a new null terminated char array
// (remember to free the returned char* after you're done with it!)
*/
char* dyn_strbuf_substr(const StrBuffer* sbuf, size_t start, size_t len)
{
    char* newstr;
    dyn_strbuf_boundscheckreadrange(sbuf, start, len);
    newstr = (char*)malloc((len + 1) * sizeof(char));
    strncpy(newstr, sbuf->m_data + start, len);
    newstr[len] = '\0';
    return newstr;
}

void dyn_strbuf_touppercase(StrBuffer* sbuf)
{
    char* pos;
    char* end;
    end = sbuf->m_data + sbuf->m_length;
    for(pos = sbuf->m_data; pos < end; pos++)
    {
        *pos = (char)toupper(*pos);
    }
}

void dyn_strbuf_tolowercase(StrBuffer* sbuf)
{
    char* pos;
    char* end;
    end = sbuf->m_data + sbuf->m_length;
    for(pos = sbuf->m_data; pos < end; pos++)
    {
        *pos = (char)tolower(*pos);
    }
}

/* Insert: copy to a StrBuffer, shifting any existing characters along */
void dyn_strbuf_insert(StrBuffer* dst, size_t dstpos, const char* src, size_t len)
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
    dst->ensureCapacityUpdatePtr(dst->m_length + len, &src);
    insert = dst->m_data + dstpos;
    /* dstpos could be at the end (== dst->m_length) */
    if(dstpos < dst->m_length)
    {
        /* Shift some characters up */
        memmove(insert + len, insert, (dst->m_length - dstpos) * sizeof(char));
        if(src >= dst->m_data && src < dst->m_data + dst->m_capacity)
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
    dst->m_length += len;
    dst->m_data[dst->m_length] = '\0';
}

/*
// Remove characters from the buffer
//   sb = ... "aaaBBccc";
//   dyn_strbuf_erase(sb, 3, 2);
//   // sb is now "aaaccc"
*/
void dyn_strbuf_erase(StrBuffer* sbuf, size_t pos, size_t len)
{
    dyn_strbuf_boundscheckreadrange(sbuf, pos, len);
    memmove(sbuf->m_data + pos, sbuf->m_data + pos + len, sbuf->m_length - pos - len);
    sbuf->m_length -= len;
    sbuf->m_data[sbuf->m_length] = '\0';
}

/* Trim whitespace characters from the start and end of a string */
void dyn_strbuf_triminplace(StrBuffer* sbuf)
{
    size_t start;
    if(sbuf->m_length == 0)
    {
        return;
    }
    /* Trim end first */
    while(sbuf->m_length > 0 && isspace((int)sbuf->m_data[sbuf->m_length - 1]))
    {
        sbuf->m_length--;
    }
    sbuf->m_data[sbuf->m_length] = '\0';
    if(sbuf->m_length == 0)
    {
        return;
    }
    start = 0;
    while(start < sbuf->m_length && isspace((int)sbuf->m_data[start]))
    {
        start++;
    }
    if(start != 0)
    {
        sbuf->m_length -= start;
        memmove(sbuf->m_data, sbuf->m_data + start, sbuf->m_length * sizeof(char));
        sbuf->m_data[sbuf->m_length] = '\0';
    }
}

/*
// Trim the characters listed in `list` from the left of `sbuf`
// `list` is a null-terminated string of characters
*/
void dyn_strbuf_trimleftinplace(StrBuffer* sbuf, const char* list)
{
    size_t start;
    start = 0;

    while(start < sbuf->m_length && (strchr(list, sbuf->m_data[start]) != NULL))
    {
        start++;
    }
    if(start != 0)
    {
        sbuf->m_length -= start;
        memmove(sbuf->m_data, sbuf->m_data + start, sbuf->m_length * sizeof(char));
        sbuf->m_data[sbuf->m_length] = '\0';
    }
}

/*
// Trim the characters listed in `list` from the right of `sbuf`
// `list` is a null-terminated string of characters
*/
void dyn_strbuf_trimrightinplace(StrBuffer* sbuf, const char* list)
{
    if(sbuf->m_length == 0)
    {
        return;
    }
    while(sbuf->m_length > 0 && strchr(list, sbuf->m_data[sbuf->m_length - 1]) != NULL)
    {
        sbuf->m_length--;
    }
    sbuf->m_data[sbuf->m_length] = '\0';
}



