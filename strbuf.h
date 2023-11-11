
#pragma once


/*
 stream_buffer.h
 project: string_buffer
 url: https://github.com/noporpoise/StringBuffer
 author: Isaac Turner <turner.isaac@gmail.com>
 license: Public Domain
 Jan 2015
*/
#include <stdbool.h>

typedef struct StringBuffer StringBuffer;

struct StringBuffer
{
    char* data;
    size_t length;// end is index of \0
    size_t capacity;// capacity should be >= end+1 to allow for \0
};

char *dyn_strutil_safencpy(char *dst, const char *src, size_t n);
size_t dyn_strutil_splitstr(char *str, char sep, char **ptrs, size_t nptrs);
size_t dyn_strutil_charreplace(char *str, char from, char to);
void dyn_strutil_reverseregion(char *str, size_t length);
bool dyn_strutil_isallspace(const char *s);
char *dyn_strutil_nextspace(char *s);
char *dyn_strutil_trim(char *str);
size_t dyn_strutil_chomp(char *str, size_t len);
size_t dyn_strutil_countchar(const char *str, char c);
size_t dyn_strutil_split(const char *splitat, const char *sourcetxt, char ***result);
StringBuffer *dyn_strbuf_makefromptr(StringBuffer *sbuf, size_t len);
StringBuffer *dyn_strbuf_makeempty(size_t len);
StringBuffer *dyn_strbuf_makefromstring(const char *str);
StringBuffer *dyn_strbuf_makeclone(const StringBuffer *sbuf);
char dyn_strbuf_resize(StringBuffer *sbuf, size_t newlen);
void dyn_strbuf_ensurecapacityupdateptr(StringBuffer *sbuf, size_t size, const char **ptr);
bool dyn_strbuf_appendnumulong(StringBuffer *buf, unsigned long value);
bool dyn_strbuf_appendnumlong(StringBuffer *buf, long value);
bool dyn_strbuf_appendnumint(StringBuffer *buf, int value);
bool dyn_strbuf_appendstrnlowercase(StringBuffer *buf, const char *str, size_t len);
bool dyn_strbuf_appendstrnuppercase(StringBuffer *buf, const char *str, size_t len);
bool dyn_strbuf_appendcharn(StringBuffer *buf, char c, size_t n);
size_t dyn_strbuf_chomp(StringBuffer *sbuf);
void dyn_strbuf_reverse(StringBuffer *sbuf);
char *dyn_strbuf_substr(const StringBuffer *sbuf, size_t start, size_t len);
void dyn_strbuf_touppercase(StringBuffer *sbuf);
void dyn_strbuf_tolowercase(StringBuffer *sbuf);
void dyn_strbuf_copyover(StringBuffer *dst, size_t dstpos, const char *src, size_t len);
void dyn_strbuf_insert(StringBuffer *dst, size_t dstpos, const char *src, size_t len);
void dyn_strbuf_overwrite(StringBuffer *dst, size_t dstpos, size_t dstlen, const char *src, size_t srclen);
void dyn_strbuf_erase(StringBuffer *sbuf, size_t pos, size_t len);
int dyn_strbuf_appendformatv(StringBuffer *sbuf, size_t pos, const char *fmt, va_list argptr);
int dyn_strbuf_appendformat(StringBuffer *sbuf, const char *fmt, ...);
int dyn_strbuf_appendformatat(StringBuffer *sbuf, size_t pos, const char *fmt, ...);
int dyn_strbuf_appendformatnoterm(StringBuffer *sbuf, size_t pos, const char *fmt, ...);
void dyn_strbuf_triminplace(StringBuffer *sbuf);
void dyn_strbuf_trimleftinplace(StringBuffer *sbuf, const char *list);
void dyn_strbuf_trimrightinplace(StringBuffer *sbuf, const char *list);
