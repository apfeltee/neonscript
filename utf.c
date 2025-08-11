
#include "neon.h"

/*
via: https://github.com/adrianwk94/utf8-iterator
UTF-8 Iterator. Version 0.1.3

Original code by Adrian Guerrero Vera (adrianwk94@gmail.com)
MIT License
Copyright (c) 2016 Adrian Guerrero Vera

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:
The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.
THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

*/

/* allows you to set a custom length. */
void nn_utf8iter_init(utf8iterator_t* iter, const char* ptr, uint32_t length)
{
    iter->plainstr = ptr;
    iter->plainlen = length;
    iter->codepoint = 0;
    iter->currpos = 0;
    iter->nextpos = 0;
    iter->currcount = 0;
}

/* calculate the number of bytes a UTF8 character occupies in a string. */
uint8_t nn_utf8iter_charsize(const char* character)
{
    if(character == NULL)
    {
        return 0;
    }
    if(character[0] == 0)
    {
        return 0;
    }
    if((character[0] & 0x80) == 0)
    {
        return 1;
    }
    else if((character[0] & 0xE0) == 0xC0)
    {
        return 2;
    }
    else if((character[0] & 0xF0) == 0xE0)
    {
        return 3;
    }
    else if((character[0] & 0xF8) == 0xF0)
    {
        return 4;
    }
    else if((character[0] & 0xFC) == 0xF8)
    {
        return 5;
    }
    else if((character[0] & 0xFE) == 0xFC)
    {
        return 6;
    }
    return 0;
}

uint32_t nn_utf8iter_converter(const char* character, uint8_t size)
{
    uint8_t i;
    static uint32_t codepoint = 0;
    static const uint8_t g_utf8iter_table_unicode[] = { 0, 0, 0x1F, 0xF, 0x7, 0x3, 0x1 };
    if(size == 0)
    {
        return 0;
    }
    if(character == NULL)
    {
        return 0;
    }
    if(character[0] == 0)
    {
        return 0;
    }
    if(size == 1)
    {
        return character[0];
    }
    codepoint = g_utf8iter_table_unicode[size] & character[0];
    for(i = 1; i < size; i++)
    {
        codepoint = codepoint << 6;
        codepoint = codepoint | (character[i] & 0x3F);
    }
    return codepoint;
}

/* returns 1 if there is a character in the next position. If there is not, return 0. */
uint8_t nn_utf8iter_next(utf8iterator_t* iter)
{
    const char* pointer;
    if(iter == NULL)
    {
        return 0;
    }
    if(iter->plainstr == NULL)
    {
        return 0;
    }
    if(iter->nextpos < iter->plainlen)
    {
        iter->currpos = iter->nextpos;
        /* Set Current Pointer */
        pointer = iter->plainstr + iter->nextpos;
        iter->charsize = nn_utf8iter_charsize(pointer);
        if(iter->charsize == 0)
        {
            return 0;
        }
        iter->nextpos = iter->nextpos + iter->charsize;
        iter->codepoint = nn_utf8iter_converter(pointer, iter->charsize);
        if(iter->codepoint == 0)
        {
            return 0;
        }
        iter->currcount++;
        return 1;
    }
    iter->currpos = iter->nextpos;
    return 0;
}

/* return current character in UFT8 - no same that iter.codepoint (not codepoint/unicode) */
const char* nn_utf8iter_getchar(utf8iterator_t* iter)
{
    uint8_t i;
    const char* pointer;
    static char str[10];
    str[0] = '\0';
    if(iter == NULL)
    {
        return str;
    }
    if(iter->plainstr == NULL)
    {
        return str;
    }
    if(iter->charsize == 0)
    {
        return str;
    }
    if(iter->charsize == 1)
    {
        str[0] = iter->plainstr[iter->currpos];
        str[1] = '\0';
        return str;
    }
    pointer = iter->plainstr + iter->currpos;
    for(i = 0; i < iter->charsize; i++)
    {
        str[i] = pointer[i];
    }
    str[iter->charsize] = '\0';
    return str;
}

/* returns the number of bytes contained in a unicode character */
int nn_util_utf8numbytes(int value)
{
    if(value < 0)
    {
        return -1;
    }
    if(value <= 0x7f)
    {
        return 1;
    }
    if(value <= 0x7ff)
    {
        return 2;
    }
    if(value <= 0xffff)
    {
        return 3;
    }
    if(value <= 0x10ffff)
    {
        return 4;
    }
    return 0;
}

char* nn_util_utf8encode(unsigned int code, size_t* dlen)
{
    int count;
    char* chars;
    *dlen = 0;
    count = nn_util_utf8numbytes((int)code);
    if(nn_util_likely(count > 0))
    {
        *dlen = count;
        chars = (char*)nn_memory_malloc(sizeof(char) * ((size_t)count + 1));
        if(chars != NULL)
        {
            if(code <= 0x7F)
            {
                chars[0] = (char)(code & 0x7F);
                chars[1] = '\0';
            }
            else if(code <= 0x7FF)
            {
                /* one continuation byte */
                chars[1] = (char)(0x80 | (code & 0x3F));
                code = (code >> 6);
                chars[0] = (char)(0xC0 | (code & 0x1F));
            }
            else if(code <= 0xFFFF)
            {
                /* two continuation bytes */
                chars[2] = (char)(0x80 | (code & 0x3F));
                code = (code >> 6);
                chars[1] = (char)(0x80 | (code & 0x3F));
                code = (code >> 6);
                chars[0] = (char)(0xE0 | (code & 0xF));
            }
            else if(code <= 0x10FFFF)
            {
                /* three continuation bytes */
                chars[3] = (char)(0x80 | (code & 0x3F));
                code = (code >> 6);
                chars[2] = (char)(0x80 | (code & 0x3F));
                code = (code >> 6);
                chars[1] = (char)(0x80 | (code & 0x3F));
                code = (code >> 6);
                chars[0] = (char)(0xF0 | (code & 0x7));
            }
            else
            {
                /* unicode replacement character */
                chars[2] = (char)0xEF;
                chars[1] = (char)0xBF;
                chars[0] = (char)0xBD;
            }
            return chars;
        }
    }
    return NULL;
}

int nn_util_utf8decode(const uint8_t* bytes, uint32_t length)
{
    int value;
    uint32_t remainingbytes;
    /* Single byte (i.e. fits in ASCII). */
    if(*bytes <= 0x7f)
    {
        return *bytes;
    }
    if((*bytes & 0xe0) == 0xc0)
    {
        /* Two byte sequence: 110xxxxx 10xxxxxx. */
        value = *bytes & 0x1f;
        remainingbytes = 1;
    }
    else if((*bytes & 0xf0) == 0xe0)
    {
        /* Three byte sequence: 1110xxxx	 10xxxxxx 10xxxxxx. */
        value = *bytes & 0x0f;
        remainingbytes = 2;
    }
    else if((*bytes & 0xf8) == 0xf0)
    {
        /* Four byte sequence: 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx. */
        value = *bytes & 0x07;
        remainingbytes = 3;
    }
    else
    {
        /* Invalid UTF-8 sequence. */
        return -1;
    }
    /* Don't read past the end of the buffer on truncated UTF-8. */
    if(remainingbytes > length - 1)
    {
        return -1;
    }
    while(remainingbytes > 0)
    {
        bytes++;
        remainingbytes--;
        /* Remaining bytes must be of form 10xxxxxx. */
        if((*bytes & 0xc0) != 0x80)
        {
            return -1;
        }
        value = value << 6 | (*bytes & 0x3f);
    }
    return value;
}

char* nn_util_utf8codepoint(const char* str, char* outcodepoint)
{
    if(0xf0 == (0xf8 & str[0]))
    {
        /* 4 byte utf8 codepoint */
        *outcodepoint = (
            ((0x07 & str[0]) << 18) |
            ((0x3f & str[1]) << 12) |
            ((0x3f & str[2]) << 6) |
            (0x3f & str[3])
        );
        str += 4;
    }
    else if(0xe0 == (0xf0 & str[0]))
    {
        /* 3 byte utf8 codepoint */
        *outcodepoint = (
            ((0x0f & str[0]) << 12) |
            ((0x3f & str[1]) << 6) |
            (0x3f & str[2])
        );
        str += 3;
    }
    else if(0xc0 == (0xe0 & str[0]))
    {
        /* 2 byte utf8 codepoint */
        *outcodepoint = (
            ((0x1f & str[0]) << 6) |
            (0x3f & str[1])
        );
        str += 2;
    }
    else
    {
        /* 1 byte utf8 codepoint otherwise */
        *outcodepoint = str[0];
        str += 1;
    }
    return (char*)str;
}

char* nn_util_utf8strstr(const char* haystack, const char* needle)
{
    char throwawaycodepoint;
    const char* n;
    const char* maybematch;
    throwawaycodepoint = 0;
    /* if needle has no utf8 codepoints before the null terminating
     * byte then return haystack */
    if('\0' == *needle)
    {
        return (char*)haystack;
    }
    while('\0' != *haystack)
    {
        maybematch = haystack;
        n = needle;
        while(*haystack == *n && (*haystack != '\0' && *n != '\0'))
        {
            n++;
            haystack++;
        }
        if('\0' == *n)
        {
            /* we found the whole utf8 string for needle in haystack at
             * maybematch, so return it */
            return (char*)maybematch;
        }
        else
        {
            /* h could be in the middle of an unmatching utf8 codepoint,
             * so we need to march it on to the next character beginning
             * starting from the current character */
            haystack = nn_util_utf8codepoint(maybematch, &throwawaycodepoint);
        }
    }
    /* no match */
    return NULL;
}

/*
// returns a pointer to the beginning of the pos'th utf8 codepoint
// in the buffer at s
*/
char* nn_util_utf8index(char* s, int pos)
{
    ++pos;
    for(; *s; ++s)
    {
        if((*s & 0xC0) != 0x80)
        {
            --pos;
        }
        if(pos == 0)
        {
            return s;
        }
    }
    return NULL;
}

/*
// converts codepoint indexes start and end to byte offsets in the buffer at s
*/
void nn_util_utf8slice(char* s, int* start, int* end)
{
    char* p;
    p = nn_util_utf8index(s, *start);
    if(p != NULL)
    {
        *start = (int)(p - s);
    }
    else
    {
        *start = -1;
    }
    p = nn_util_utf8index(s, *end);
    if(p != NULL)
    {
        *end = (int)(p - s);
    }
    else
    {
        *end = (int)strlen(s);
    }
}

