
#pragma once

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

#include <stdint.h>
#include <string.h>

#if defined(__STRICT_ANSI__)
    #define UTF8ITER_INLINE
#else
    #define UTF8ITER_INLINE inline
#endif

typedef struct utf8iterator_t utf8iterator_t;
struct utf8iterator_t
{
    const char* ptr;
    uint32_t codepoint;
    /* character size in bytes */
    uint8_t size;
    /* current character position */
    uint32_t position;
    /* next character position */
    uint32_t next;
    /* number of counter characters currently */
    uint32_t count;
    /* strlen() */
    uint32_t length;
};

static const uint8_t g_utf8iter_table_unicode[] = { 0, 0, 0x1F, 0xF, 0x7, 0x3, 0x1 };

/* allows you to set a custom length. */
static UTF8ITER_INLINE void utf8_init(utf8iterator_t* iter, const char* ptr, uint32_t length)
{
    iter->ptr = ptr;
    iter->codepoint = 0;
    iter->position = 0;
    iter->next = 0;
    iter->count = 0;
    iter->length = length;
}

/* calculate the number of bytes a UTF8 character occupies in a string. */
static UTF8ITER_INLINE uint8_t utf8_charsize(const char* character)
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

static UTF8ITER_INLINE uint32_t utf8_converter(const char* character, uint8_t size)
{
    uint8_t i;
    static uint32_t codepoint;
    codepoint = 0;
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
static UTF8ITER_INLINE uint8_t utf8_next(utf8iterator_t* iter)
{
    const char* pointer;
    if(iter == NULL)
    {
        return 0;
    }
    if(iter->ptr == NULL)
    {
        return 0;
    }
    if(iter->next < iter->length)
    {
        iter->position = iter->next;
        /* Set Current Pointer */
        pointer = iter->ptr + iter->next;
        iter->size = utf8_charsize(pointer);
        if(iter->size == 0)
        {
            return 0;
        }
        iter->next = iter->next + iter->size;
        iter->codepoint = utf8_converter(pointer, iter->size);
        if(iter->codepoint == 0)
        {
            return 0;
        }
        iter->count++;
        return 1;
    }
    iter->position = iter->next;
    return 0;
}

/* return current character in UFT8 - no same that iter.codepoint (not codepoint/unicode) */
static UTF8ITER_INLINE const char* utf8_getchar(utf8iterator_t* iter)
{
    uint8_t i;
    const char* pointer;
    static char str[10];
    str[0] = '\0';
    if(iter == NULL)
    {
        return str;
    }
    if(iter->ptr == NULL)
    {
        return str;
    }
    if(iter->size == 0)
    {
        return str;
    }
    if(iter->size == 1)
    {
        str[0] = iter->ptr[iter->position];
        str[1] = '\0';
        return str;
    }
    pointer = iter->ptr + iter->position;
    for(i = 0; i < iter->size; i++)
    {
        str[i] = pointer[i];
    }
    str[iter->size] = '\0';
    return str;
}

