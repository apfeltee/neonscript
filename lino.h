
#if !defined(__NNLINOHEADERFILE_H__)
#define __NNLINOHEADERFILE_H__
/* lino.h -- based on linenoise VERSION 1.0
 *
 * Guerrilla line editing library against the idea that a line editing lib
 * needs to be 20,000 lines of C code.
 *
 * See linenoise.c for more information.
 *
 * ------------------------------------------------------------------------
 *
 * Copyright (c) 2010-2014, Salvatore Sanfilippo <antirez at gmail dot com>
 * Copyright (c) 2010-2013, Pieter Noordhuis <pcnoordhuis at gmail dot com>
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *  *  Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *
 *  *  Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdint.h>
#include <stddef.h>

typedef struct linobuffer_t linobuffer_t;
typedef struct linocompletions_t linocompletions_t;
typedef struct linostate_t linostate_t; 
typedef void(linofncompletionfunc_t)(const char *, linocompletions_t *);
typedef char*(linofnhintsfunc_t)(const char *, int *color, int *bold);
typedef void(linofnfreehintsfunc_t)(void *);

typedef size_t (linofnprevcharlenfunc_t)(const char *buf, size_t buf_len, size_t pos, size_t *col_len);
typedef size_t (linofnnextcharlenfunc_t)(const char *buf, size_t buf_len, size_t pos, size_t *col_len);
typedef size_t (linofnreadcodefunc_t)(int fd, char *buf, size_t buf_len, int* c);


struct linobuffer_t
{
    char* b;
    int len;
};


/* The linostate_t structure represents the state during line editing.
 * We pass this state to functions implementing specific editing
 * functionalities. */
struct linostate_t
{
    int ifd; /* Terminal stdin file descriptor. */
    int ofd; /* Terminal stdout file descriptor. */
    char* buf; /* Edited line buffer. */
    size_t buflen; /* Edited line buffer size. */
    const char* prompt; /* Prompt to display. */
    size_t plen; /* Prompt length. */
    size_t pos; /* Current cursor position. */
    size_t oldcolpos; /* Previous refresh cursor column position. */
    size_t len; /* Current edited line length. */
    size_t cols; /* Number of columns in terminal. */
    size_t maxrows; /* Maximum num of rows used so far (multiline mode) */
    int history_index; /* The history index we are currently editing. */
};

enum
{
    LINO_KEY_NULL = 0, /* NULL */
    LINO_KEY_CTRLA = 1, /* Ctrl+a */
    LINO_KEY_CTRLB = 2, /* Ctrl-b */
    LINO_KEY_CTRLC = 3, /* Ctrl-c */
    LINO_KEY_CTRLD = 4, /* Ctrl-d */
    LINO_KEY_CTRLE = 5, /* Ctrl-e */
    LINO_KEY_CTRLF = 6, /* Ctrl-f */
    LINO_KEY_CTRLH = 8, /* Ctrl-h */
    LINO_KEY_TAB = 9, /* Tab */
    LINO_KEY_LINEFEED = 10, /* Line feed */
    LINO_KEY_CTRLK = 11, /* Ctrl+k */
    LINO_KEY_CTRLL = 12, /* Ctrl+l */
    LINO_KEY_ENTER = 13, /* Enter */
    LINO_KEY_CTRLN = 14, /* Ctrl-n */
    LINO_KEY_CTRLP = 16, /* Ctrl-p */
    LINO_KEY_CTRLT = 20, /* Ctrl-t */
    LINO_KEY_CTRLU = 21, /* Ctrl+u */
    LINO_KEY_CTRLW = 23, /* Ctrl+w */
    LINO_KEY_ESC = 27, /* Escape */
    LINO_KEY_BACKSPACE = 127 /* Backspace */
};

struct linocompletions_t
{
    size_t len;
    char **cvec;
};

int lino_util_strcasecmp(const char *s1, const char *s2);
int lino_util_strncasecmp(const char *s1, const char *s2, int n);
size_t lino_util_defaultprevcharlen(const char *buf, size_t buf_len, size_t pos, size_t *col_len);
size_t lino_util_defaultnextcharlen(const char *buf, size_t buf_len, size_t pos, size_t *col_len);
size_t lino_util_defaultreadcode(int fd, char *buf, size_t buf_len, int *c);
void lino_setencodingfunctions(linofnprevcharlenfunc_t *prevCharLenFunc, linofnnextcharlenfunc_t *nextCharLenFunc, linofnreadcodefunc_t *readCodeFunc);
size_t lino_util_columnpos(const char *buf, size_t buf_len, size_t pos);
size_t lino_util_columnposformultiline(const char *buf, size_t buf_len, size_t pos, size_t cols, size_t ini_pos);
void lino_maskmodeenable(void);
void lino_maskmodedisable(void);
void lino_setmultiline(int ml);
int lino_util_isunsupportedterm(void);
int lino_util_enablerawmode(int fd);
void lino_util_disablerawmode(int fd);
int lino_util_getcursorposition(int ifd, int ofd);
int lino_util_getcolumns(int ifd, int ofd);
void lino_clearscreen(void);
void lino_util_beep(void);
void lino_freecompletions(linocompletions_t *lc);
int lino_completeline(linostate_t *ls, char *cbuf, size_t cbuf_len, int *c);
void lino_setcompletioncallback(linofncompletionfunc_t *fn);
void lino_sethintscallback(linofnhintsfunc_t *fn);
void lino_setfreehintscallback(linofnfreehintsfunc_t *fn);
void lino_addcompletion(linocompletions_t *lc, const char *str);
void lino_appendbuf_init(linobuffer_t *ab);
void lino_appendbuf_append(linobuffer_t *ab, const char *s, int len);
void lino_appendbuf_destroy(linobuffer_t *ab);
void lino_refreshshowhints(linobuffer_t *ab, linostate_t *l, int pcollen);
int lino_util_isansiescape(const char *buf, size_t buf_len, size_t *len);
size_t lino_util_prompttextcolumnlen(const char *prompt, size_t plen);
void lino_util_refreshsingleline(linostate_t *l);
void lino_util_refreshmultiline(linostate_t *l);
void lino_refreshline(linostate_t *l);
int lino_editinsert(linostate_t *l, const char *cbuf, int clen);
void lino_editmoveleft(linostate_t *l);
void lino_editmoveright(linostate_t *l);
void lino_editmovehome(linostate_t *l);
void lino_edit_moveend(linostate_t *l);
void lino_edithistorynext(linostate_t *l, int dir);
void lino_editdelete(linostate_t *l);
void lino_editbackspace(linostate_t *l);
void lino_editdelprevword(linostate_t *l);
int lino_editline(int stdin_fd, int stdout_fd, char *buf, size_t buflen, const char *prompt);
void lino_printkeycodes(void);
int lino_util_getraw(char *buf, size_t buflen, const char *prompt);
char *lino_notty(void);
char *lino_readline(const char *prompt);
void lino_freeline(void *ptr);
void lino_freehistory(void);
void lino_atexit(void);
int lino_historyadd(const char *line);
int lino_historysetmaxlength(int len);
int lino_historysavetofile(const char *filename);
int lino_historyloadfromfile(const char *filename);

#endif

