
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


#if defined(__GNUC__)
    #define DYN_STRBUF_ATTRIBUTE(x) __attribute__(x)
#else
    #define DYN_STRBUF_ATTRIBUTE(x)
#endif


typedef struct StringBuffer StringBuffer;

struct StringBuffer
{
    /* total length of this buffer */
    size_t length;

    /* capacity should be >= length+1 to allow for \0 */
    size_t capacity;

    char* data;
};


size_t dyn_strutil_rndup2pow64(uint64_t x);
char *dyn_strutil_safencpy(char *dst, const char *src, size_t n);
size_t dyn_strutil_splitstr(char *str, char sep, char **ptrs, size_t nptrs);
size_t dyn_strutil_charreplace(char *str, char from, char to);
void dyn_strutil_reverseregion(char *str, size_t len);
bool dyn_strutil_isallspace(const char *s);
char *dyn_strutil_nextspace(char *s);
char *dyn_strutil_trim(char *str);
size_t dyn_strutil_chomp(char *str, size_t len);
size_t dyn_strutil_countchar(const char *str, char c);
size_t dyn_strutil_split(const char *splitat, const char *sourcetxt, char ***result);
void dyn_strutil_callboundscheckinsert(const StringBuffer *sbuf, size_t pos, const char *file, int line);
void dyn_strutil_callboundscheckreadrange(const StringBuffer *sbuf, size_t start, size_t len, const char *file, int line);
StringBuffer *dyn_strbuf_makefromptr(StringBuffer *sbuf, size_t len);
StringBuffer *dyn_strbuf_makebasicempty(size_t len);
bool dyn_strbuf_destroy(StringBuffer *sb);
bool dyn_strbuf_destroyfromstack(StringBuffer* sb);
bool dyn_strbuf_destroyfromptr(StringBuffer *sb);
StringBuffer *dyn_strbuf_makefromstring(const char *str, size_t slen);
StringBuffer *dyn_strbuf_makeclone(const StringBuffer *sbuf);
void dyn_strbuf_reset(StringBuffer *sb);
void dyn_strutil_cbufcapacity(char **buf, size_t *sizeptr, size_t len);
void dyn_strutil_cbufappendchar(char **buf, size_t *lenptr, size_t *sizeptr, char c);
bool dyn_strbuf_resize(StringBuffer *sbuf, size_t newlen);
void dyn_strbuf_ensurecapacity(StringBuffer *sb, size_t len);
void dyn_strbuf_ensurecapacityupdateptr(StringBuffer *sbuf, size_t size, const char **ptr);
bool dyn_strbuf_containschar(StringBuffer *sb, char ch);
void dyn_strutil_faststrncat(char *dest, const char *src, size_t *size);
size_t dyn_strutil_strreplace1(char **str, size_t selflen, const char *findstr, size_t findlen, const char *substr, size_t sublen);
size_t dyn_strutil_strrepcount(const char *str, size_t slen, const char *findstr, size_t findlen, size_t sublen);
void dyn_strutil_strreplace2(char *target, size_t tgtlen, const char *findstr, size_t findlen, const char *substr, size_t sublen);
bool dyn_strbuf_fullreplace(StringBuffer *sb, const char *findstr, size_t findlen, const char *substr, size_t sublen);
bool dyn_strutil_inpreplhelper(char *dest, const char *src, size_t srclen, int findme, const char *substr, size_t sublen, size_t maxlen, size_t *dlen);
size_t dyn_strutil_inpreplace(char *target, size_t tgtlen, int findme, const char *substr, size_t sublen, size_t maxlen);
bool dyn_strbuf_charreplace(StringBuffer *sb, int findme, const char *substr, size_t sublen);
void dyn_strbuf_set(StringBuffer *sb, const char *str);
void dyn_strbuf_setbuff(StringBuffer *dest, StringBuffer *from);
bool dyn_strbuf_appendchar(StringBuffer *sb, int c);
bool dyn_strbuf_appendstrn(StringBuffer *sb, const char *str, size_t len);
bool dyn_strbuf_appendstr(StringBuffer *sb, const char *str);
bool dyn_strbuf_appendbuff(StringBuffer *sb1, const StringBuffer *sb2);
size_t dyn_strutil_numofdigits(unsigned long v);
bool dyn_strbuf_appendnumulong(StringBuffer *buf, unsigned long value);
bool dyn_strbuf_appendnumlong(StringBuffer *buf, long value);
bool dyn_strbuf_appendnumint(StringBuffer *buf, int value);
bool dyn_strbuf_appendstrnlowercase(StringBuffer *buf, const char *str, size_t len);
bool dyn_strbuf_appendstrnuppercase(StringBuffer *buf, const char *str, size_t len);
bool dyn_strbuf_appendcharn(StringBuffer *buf, char c, size_t n);
void dyn_strbuf_shrink(StringBuffer *sb, size_t len);
size_t dyn_strbuf_chomp(StringBuffer *sbuf);
void dyn_strbuf_reverse(StringBuffer *sbuf);
char *dyn_strbuf_substr(const StringBuffer *sbuf, size_t start, size_t len);
void dyn_strbuf_touppercase(StringBuffer *sbuf);
void dyn_strbuf_tolowercase(StringBuffer *sbuf);
void dyn_strbuf_copyover(StringBuffer *dst, size_t dstpos, const char *src, size_t len);
void dyn_strbuf_insert(StringBuffer *dst, size_t dstpos, const char *src, size_t len);
void dyn_strbuf_overwrite(StringBuffer *dst, size_t dstpos, size_t dstlen, const char *src, size_t srclen);
void dyn_strbuf_erase(StringBuffer *sbuf, size_t pos, size_t len);
int dyn_strbuf_appendformatposv(StringBuffer *sbuf, size_t pos, const char *fmt, va_list argptr);
int dyn_strbuf_appendformatv(StringBuffer *sbuf, const char *fmt, va_list argptr);
int dyn_strbuf_appendformat(StringBuffer *sbuf, const char *fmt, ...);
int dyn_strbuf_appendformatat(StringBuffer *sbuf, size_t pos, const char *fmt, ...);
int dyn_strbuf_appendformatnoterm(StringBuffer *sbuf, size_t pos, const char *fmt, ...);
void dyn_strbuf_triminplace(StringBuffer *sbuf);
void dyn_strbuf_trimleftinplace(StringBuffer *sbuf, const char *list);
void dyn_strbuf_trimrightinplace(StringBuffer *sbuf, const char *list);
