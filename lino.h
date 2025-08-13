
#if !defined(__NNLINOHEADERFILE_H__)
#define __NNLINOHEADERFILE_H__
/* lino.h -- based on linenoise VERSION 1.0 */
/* linenoise.c -- guerrilla line editing library against the idea that a
 * line editing lib needs to be 20,000 lines of C code.
 *
 * You can find the latest source code at:
 *
 *   http://github.com/antirez/linenoise
 *
 * Does a number of crazy assumptions that happen to be true in 99.9999% of
 * the 2010 UNIX computers around.
 *
 * ------------------------------------------------------------------------
 *
 * Copyright (c) 2010-2016, Salvatore Sanfilippo <antirez at gmail dot com>
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
 *
 * ------------------------------------------------------------------------
 *
 * References:
 * - http://invisible-island.net/xterm/ctlseqs/ctlseqs.html
 * - http://www.3waylabs.com/nw/WWW/products/wizcon/vt220.html
 *
 * Todo list:
 * - Filter bogus Ctrl+<char> combinations.
 * - Win32 support
 *
 * Bloat:
 * - History search like Ctrl+r in readline?
 *
 * List of escape sequences used by this program, we do everything just
 * with three sequences. In order to be so cheap we may have some
 * flickering effect with some slow terminal, but the lesser sequences
 * the more compatible.
 *
 * EL (Erase Line)
 *    Sequence: ESC [ n K
 *    Effect: if n is 0 or missing, clear from cursor to end of line
 *    Effect: if n is 1, clear from beginning of line to cursor
 *    Effect: if n is 2, clear entire line
 *
 * CUF (CUrsor Forward)
 *    Sequence: ESC [ n C
 *    Effect: moves cursor forward n chars
 *
 * CUB (CUrsor Backward)
 *    Sequence: ESC [ n D
 *    Effect: moves cursor backward n chars
 *
 * The following is used to get the terminal width if getting
 * the width with the TIOCGWINSZ ioctl fails
 *
 * DSR (Device Status Report)
 *    Sequence: ESC [ 6 n
 *    Effect: reports the current cusor position as ESC [ n ; m R
 *            where n is the row and m is the column
 *
 * When multi line mode is enabled, we also use an additional escape
 * sequence. However multi line editing is disabled by default.
 *
 * CUU (Cursor Up)
 *    Sequence: ESC [ n A
 *    Effect: moves cursor up of n chars.
 *
 * CUD (Cursor Down)
 *    Sequence: ESC [ n B
 *    Effect: moves cursor down of n chars.
 *
 * When lino_clearscreen() is called, two additional escape sequences
 * are used in order to clear the screen and position the cursor at home
 * position.
 *
 * CUP (Cursor position)
 *    Sequence: ESC [ H
 *    Effect: moves the cursor to upper left corner
 *
 * ED (Erase display)
 *    Sequence: ESC [ 2 J
 *    Effect: clear the whole screen
 *
 */

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <assert.h>

#if defined(__linux__) || defined(__unix__)
    #include <sys/ioctl.h>
    #include <termios.h>
    #include <unistd.h>
#endif

#define LINENOISE_DEFAULT_HISTORY_MAX_LEN 100
#define LINENOISE_MAX_LINE 4096

#if defined(__linux__) || defined(__unix__)
    #define LINENOISE_ISUNIX
#endif

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

typedef struct linocontext_t linocontext_t;
typedef struct linobuffer_t linobuffer_t;
typedef struct linocompletions_t linocompletions_t;
typedef struct linoeditstate_t linoeditstate_t;

typedef void(linofncomp_t)(linocontext_t* ctx, const char *, linocompletions_t *);
typedef char*(linofnhint_t)(linocontext_t* ctx, const char *, int *color, int *bold);
typedef void(linofnhintfree_t)(linocontext_t* ctx, void *);

typedef size_t (linofnprevchlen_t)(linocontext_t* ctx, const char *buf, size_t buf_len, size_t pos, size_t *col_len);
typedef size_t (linofnnextchlen_t)(linocontext_t* ctx, const char *buf, size_t buf_len, size_t pos, size_t *col_len);
typedef size_t (linofnreadcode_t)(linocontext_t* ctx, int fd, char *buf, size_t buf_len, int* c);

struct linobuffer_t
{
    char* bufdata;
    int buflen;
};

/* The linoeditstate_t structure represents the state during line editing.
 * We pass this state to functions implementing specific editing
 * functionalities. */
struct linoeditstate_t
{
    int termstdinfd; /* Terminal stdin file descriptor. */
    int termstdoutfd; /* Terminal stdout file descriptor. */
    char* edlinebuf; /* Edited line buffer. */
    size_t edlinelen; /* Edited line buffer size. */
    const char* promptdata; /* Prompt to display. */
    size_t promptlen; /* Prompt length. */
    size_t currentcursorpos; /* Current cursor position. */
    size_t prevrefreshcursorpos; /* Previous refresh cursor column position. */
    size_t currentedlinelen; /* Current edited line length. */
    size_t terminalcolumns; /* Number of columns in terminal. */
    size_t maxrowsused; /* Maximum num of rows used so far (multiline mode) */
    int historyindex; /* The history index we are currently editing. */
};

struct linocompletions_t
{
    size_t count;
    char** cvec;
};

struct linocontext_t
{
    linofncomp_t* completioncallback;
    linofnhint_t* hintscallback;
    linofnhintfree_t* freehintscallback;

    /* Set default encoding functions */
    linofnprevchlen_t* fnprevcharlen;
    linofnnextchlen_t* fnnextcharlen;
    linofnreadcode_t* fnreadcode;

    #if defined(LINENOISE_ISUNIX)
    struct termios origtermios; /* In order to restore at exit.*/
    #endif
    int maskmode; /* Show "***" instead of input. For passwords. */
    int israwmode; /* For atexit() function to check if restore is needed*/
    int ismultilinemode; /* Multi line mode. Default is single line. */
    int atexitregistered; /* Register atexit just 1 time. */
    int historymaxlen;
    int historylength;
    char** historybuflines;
};

static linocontext_t* g_linoconst_gcontext = NULL;
static const char* g_linoconst_unsupportedterminals[] = { "dumb", "cons25", "emacs", NULL };

/* Debugging macro. */
#if 0
FILE *lndebug_fp = NULL;
    #define lndebug(...)                                                                                                                                    \
        do                                                                                                                                                  \
        {                                                                                                                                                   \
            if(lndebug_fp == NULL)                                                                                                                          \
            {                                                                                                                                               \
                lndebug_fp = fopen("/tmp/lndebug.txt", "a");                                                                                                \
                fprintf(lndebug_fp, "[%d %d %d] p: %d, rows: %d, rpos: %d, max: %d, oldmax: %d\n", (int)edst->currentedlinelen, (int)edst->currentcursorpos, (int)edst->prevrefreshcursorpos, plen, rows, \
                        rpos, (int)edst->maxrowsused, oldrows);                                                                                                   \
            }                                                                                                                                               \
            fprintf(lndebug_fp, ", " __VA_ARGS__);                                                                                                          \
            fflush(lndebug_fp);                                                                                                                             \
        } while(0)
#else
    #define lndebug(fmt, ...)
#endif


/* ========================== Encoding functions ============================= */

static const char g_linoconst_strcmpcharmap[] = {
	'\000', '\001', '\002', '\003', '\004', '\005', '\006', '\007',
	'\010', '\011', '\012', '\013', '\014', '\015', '\016', '\017',
	'\020', '\021', '\022', '\023', '\024', '\025', '\026', '\027',
	'\030', '\031', '\032', '\033', '\034', '\035', '\036', '\037',
	'\040', '\041', '\042', '\043', '\044', '\045', '\046', '\047',
	'\050', '\051', '\052', '\053', '\054', '\055', '\056', '\057',
	'\060', '\061', '\062', '\063', '\064', '\065', '\066', '\067',
	'\070', '\071', '\072', '\073', '\074', '\075', '\076', '\077',
	'\100', '\141', '\142', '\143', '\144', '\145', '\146', '\147',
	'\150', '\151', '\152', '\153', '\154', '\155', '\156', '\157',
	'\160', '\161', '\162', '\163', '\164', '\165', '\166', '\167',
	'\170', '\171', '\172', '\133', '\134', '\135', '\136', '\137',
	'\140', '\141', '\142', '\143', '\144', '\145', '\146', '\147',
	'\150', '\151', '\152', '\153', '\154', '\155', '\156', '\157',
	'\160', '\161', '\162', '\163', '\164', '\165', '\166', '\167',
	'\170', '\171', '\172', '\173', '\174', '\175', '\176', '\177',
	'\200', '\201', '\202', '\203', '\204', '\205', '\206', '\207',
	'\210', '\211', '\212', '\213', '\214', '\215', '\216', '\217',
	'\220', '\221', '\222', '\223', '\224', '\225', '\226', '\227',
	'\230', '\231', '\232', '\233', '\234', '\235', '\236', '\237',
	'\240', '\241', '\242', '\243', '\244', '\245', '\246', '\247',
	'\250', '\251', '\252', '\253', '\254', '\255', '\256', '\257',
	'\260', '\261', '\262', '\263', '\264', '\265', '\266', '\267',
	'\270', '\271', '\272', '\273', '\274', '\275', '\276', '\277',
	'\300', '\341', '\342', '\343', '\344', '\345', '\346', '\347',
	'\350', '\351', '\352', '\353', '\354', '\355', '\356', '\357',
	'\360', '\361', '\362', '\363', '\364', '\365', '\366', '\367',
	'\370', '\371', '\372', '\333', '\334', '\335', '\336', '\337',
	'\340', '\341', '\342', '\343', '\344', '\345', '\346', '\347',
	'\350', '\351', '\352', '\353', '\354', '\355', '\356', '\357',
	'\360', '\361', '\362', '\363', '\364', '\365', '\366', '\367',
	'\370', '\371', '\372', '\373', '\374', '\375', '\376', '\377',
};

static void lino_context_init(linocontext_t *ctx);
static int lino_util_strcasecmp(const char *s1, const char *s2);
static size_t lino_util_defaultprevcharlen(linocontext_t *ctx, const char *buf, size_t buflen, size_t pos, size_t *collen);
static size_t lino_util_defaultnextcharlen(linocontext_t *ctx, const char *buf, size_t buflen, size_t pos, size_t *collen);
static size_t lino_util_defaultreadcode(linocontext_t *ctx, int fd, char *buf, size_t buflen, int *c);
static void lino_context_setencodingfunctions(linocontext_t *ctx, linofnprevchlen_t *pclfunc, linofnnextchlen_t *nclfunc, linofnreadcode_t *rcfunc);
static size_t lino_context_columnpos(linocontext_t *ctx, const char *buf, size_t buflen, size_t pos);
static size_t lino_context_columnposformultiline(linocontext_t *ctx, const char *buf, size_t buflen, size_t pos, size_t cols, size_t ini_pos);
static void lino_context_maskmodeenable(linocontext_t *ctx);
static void lino_context_maskmodedisable(linocontext_t *ctx);
static void lino_context_setmultiline(linocontext_t *ctx, int ml);
static int lino_util_isunsupportedterm(void);
static int lino_context_enablerawmode(linocontext_t *ctx, int fd);
static void lino_context_disablerawmode(linocontext_t *ctx, int fd);
static int lino_util_getcursorposition(int ifd, int ofd);
static int lino_util_getcolumns(int ifd, int ofd);
static void lino_clearscreen(void);
static void lino_util_beep(void);
static void lino_completions_destroy(linocompletions_t *lc);
static int lino_completeline(linocontext_t *ctx, linoeditstate_t *ls, char *cbuf, size_t cbuflen, int *c);
static void lino_setcompletioncallback(linocontext_t *ctx, linofncomp_t *fn);
static void lino_sethintscallback(linocontext_t *ctx, linofnhint_t *fn);
static void lino_setfreehintscallback(linocontext_t *ctx, linofnhintfree_t *fn);
static void lino_addcompletion(linocontext_t *ctx, linocompletions_t *lc, const char *str);
static void lino_appendbuf_init(linobuffer_t *ab);
static void lino_appendbuf_append(linobuffer_t *ab, const char *s, int len);
static void lino_appendbuf_destroy(linobuffer_t *ab);
static void lino_refreshshowhints(linocontext_t *ctx, linobuffer_t *ab, linoeditstate_t *edst, int pcollen);
static int lino_util_isansiescape(const char *buf, size_t buflen, size_t *len);
static size_t lino_util_prompttextcolumnlen(linocontext_t *ctx, const char *prompt, size_t plen);
static void lino_context_refreshsingleline(linocontext_t *ctx, linoeditstate_t *edst);
static void lino_context_refreshmultiline(linocontext_t *ctx, linoeditstate_t *edst);
static void lino_context_refreshline(linocontext_t *ctx, linoeditstate_t *edst);
static int lino_edit_insert(linocontext_t *ctx, linoeditstate_t *edst, const char *cbuf, int clen);
static void lino_edit_moveleft(linocontext_t *ctx, linoeditstate_t *edst);
static void lino_edit_moveright(linocontext_t *ctx, linoeditstate_t *edst);
static void lino_edit_movehome(linocontext_t *ctx, linoeditstate_t *edst);
static void lino_edit_moveend(linocontext_t *ctx, linoeditstate_t *edst);
static void lino_edit_historynext(linocontext_t *ctx, linoeditstate_t *edst, int dir);
static void lino_edit_delete(linocontext_t *ctx, linoeditstate_t *edst);
static void lino_edit_backspace(linocontext_t *ctx, linoeditstate_t *edst);
static void lino_edit_delprevword(linocontext_t *ctx, linoeditstate_t *edst);
static int lino_edit_editline(linocontext_t *ctx, int stdin_fd, int stdout_fd, char *buf, size_t buflen, const char *prompt);
static void lino_debug_printkeycodes(linocontext_t *ctx);
static int lino_context_getraw(linocontext_t *ctx, char *buf, size_t buflen, const char *prompt);
static char *lino_context_fallbacknotty(linocontext_t *ctx);
static char *lino_context_readline(linocontext_t *ctx, const char *prompt);
static void lino_context_freeline(linocontext_t *ctx, void *ptr);
static void lino_context_freehistory(linocontext_t *ctx);
static void lino_context_handleimplicitexit(void);
static int lino_context_historyadd(linocontext_t *ctx, const char *line);
static int lino_context_historysetmaxlength(linocontext_t *ctx, int len);
static int lino_context_historysavetofile(linocontext_t *ctx, const char *filename);
static int lino_context_historyloadfromfile(linocontext_t *ctx, const char *filename);

static void lino_context_init(linocontext_t* ctx)
{
    memset(ctx, 0, sizeof(linocontext_t));
    ctx->completioncallback = NULL;
    ctx->hintscallback = NULL;
    ctx->freehintscallback = NULL;
    ctx->maskmode = 0; /* Show "***" instead of input. For passwords. */
    ctx->israwmode = 0; /* For atexit() function to check if restore is needed*/
    ctx->ismultilinemode = 0; /* Multi line mode. Default is single line. */
    ctx->atexitregistered = 0; /* Register atexit just 1 time. */
    ctx->historymaxlen = LINENOISE_DEFAULT_HISTORY_MAX_LEN;
    ctx->historylength = 0;
    ctx->historybuflines = NULL;
    /* Set default encoding functions */
    ctx->fnprevcharlen = lino_util_defaultprevcharlen;
    ctx->fnnextcharlen = lino_util_defaultnextcharlen;
    ctx->fnreadcode = lino_util_defaultreadcode;
    g_linoconst_gcontext = ctx;
}

static int lino_util_strcasecmp(const char* s1, const char* s2)
{
    const char *cm;
    cm = g_linoconst_strcmpcharmap;
	while (cm[(int)*s1] == cm[(int)*s2++])
    {
		if (*s1++ == '\0')
        {
            return(0);
        }
	}
    return(cm[(int)*s1] - cm[(int)*--s2]);
}

/* Get byte length and column length of the previous character */
static size_t lino_util_defaultprevcharlen(linocontext_t* ctx, const char* buf, size_t buflen, size_t pos, size_t* collen)
{
    (void)ctx;
    (void)buf;
    (void)buflen;
    (void)pos;
    if(collen != NULL)
    {
        *collen = 1;
    }
    return 1;
}

/* Get byte length and column length of the next character */
static size_t lino_util_defaultnextcharlen(linocontext_t* ctx, const char* buf, size_t buflen, size_t pos, size_t* collen)
{
    (void)ctx;
    (void)buf;
    (void)buflen;
    (void)pos;
    if(collen != NULL)
    {
        *collen = 1;
    }
    return 1;
}

/* Read bytes of the next character */
static size_t lino_util_defaultreadcode(linocontext_t* ctx, int fd, char* buf, size_t buflen, int* c)
{
    int nread;
    (void)ctx;
    if(buflen < 1)
    {
        return -1;
    }
    nread = read(fd, &buf[0], 1);
    if(nread == 1)
    {
        *c = buf[0];
    }
    return nread;
}

/* Set used defined encoding functions */
static void lino_context_setencodingfunctions(linocontext_t* ctx, linofnprevchlen_t* pclfunc, linofnnextchlen_t* nclfunc, linofnreadcode_t* rcfunc)
{
    ctx->fnprevcharlen = pclfunc;
    ctx->fnnextcharlen = nclfunc;
    ctx->fnreadcode = rcfunc;
}

/* Get column length from begining of buffer to current byte position */
static size_t lino_context_columnpos(linocontext_t* ctx, const char* buf, size_t buflen, size_t pos)
{
    size_t ret;
    size_t off;
    size_t len;
    size_t collen;
    ret = 0;
    off = 0;
    while(off < pos)
    {
        len = ctx->fnnextcharlen(ctx, buf, buflen, off, &collen);
        off += len;
        ret += collen;
    }
    return ret;
}

/* Get column length from begining of buffer to current byte position for multiline mode*/
static size_t lino_context_columnposformultiline(linocontext_t* ctx, const char* buf, size_t buflen, size_t pos, size_t cols, size_t ini_pos)
{
    int dif;
    size_t off;
    size_t ret;
    size_t len;
    size_t colwid;
    size_t collen;
    ret = 0;
    colwid = ini_pos;
    off = 0;
    while(off < buflen)
    {
        len = ctx->fnnextcharlen(ctx, buf, buflen, off, &collen);
        dif = (int)(colwid + collen) - (int)cols;
        if(dif > 0)
        {
            ret += dif;
            colwid = collen;
        }
        else if(dif == 0)
        {
            colwid = 0;
        }
        else
        {
            colwid += collen;
        }
        if(off >= pos)
        {
            break;
        }
        off += len;
        ret += collen;
    }
    return ret;
}

/* ======================= Low level terminal handling ====================== */

/* Enable "mask mode". When it is enabled, instead of the input that
 * the user is typing, the terminal will just display a corresponding
 * number of asterisks, like "****". This is useful for passwords and other
 * secrets that should not be displayed. */
static void lino_context_maskmodeenable(linocontext_t* ctx)
{
    ctx->maskmode = 1;
}

/* Disable mask mode. */
static void lino_context_maskmodedisable(linocontext_t* ctx)
{
    ctx->maskmode = 0;
}

/* Set if to use or not the multi line mode. */
static void lino_context_setmultiline(linocontext_t* ctx, int ml)
{
    ctx->ismultilinemode = ml;
}

/* Return true if the terminal name is in the list of terminals we know are
 * not able to understand basic escape sequences. */
static int lino_util_isunsupportedterm(void)
{
    int j;
    char* term;
    #if !defined(LINENOISE_ISUNIX)
        return 1;
    #endif
    term = getenv("TERM");
    if(term == NULL)
    {
        return 0;
    }
    for(j = 0; g_linoconst_unsupportedterminals[j]; j++)
    {
        if(!lino_util_strcasecmp(term, g_linoconst_unsupportedterminals[j]))
        {
            return 1;
        }
    }
    return 0;
}

/* Raw mode: 1960 magic shit. */
static int lino_context_enablerawmode(linocontext_t* ctx, int fd)
{
    #if defined(LINENOISE_ISUNIX)
    struct termios raw;
    if(!isatty(fileno(stdin)))
    {
        goto fatal;
    }
    if(!ctx->atexitregistered)
    {
        atexit(lino_context_handleimplicitexit);
        ctx->atexitregistered = 1;
    }
    if(tcgetattr(fd, &ctx->origtermios) == -1)
    {
        goto fatal;
    }
    raw = ctx->origtermios; /* modify the original mode */
    /* input modes: no break, no CR to NL, no parity check, no strip char,
   * no start/stop output control. */
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    /* output modes - disable post processing */
    raw.c_oflag &= ~(OPOST);
    /* control modes - set 8 bit chars */
    raw.c_cflag |= (CS8);
    /* local modes - choing off, canonical off, no extended functions,
   * no signal chars (^Z,^C) */
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    /* control chars - set return condition: min number of bytes and timer.
   * We want read to return every single byte, without timeout. */
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0; /* 1 byte, no timer */
    /* put terminal in raw mode after flushing */
    if(tcsetattr(fd, TCSADRAIN, &raw) < 0)
    {
        goto fatal;
    }
    ctx->israwmode = 1;
    return 0;
fatal:
    errno = ENOTTY;
    #endif
    return -1;
}

static void lino_context_disablerawmode(linocontext_t* ctx, int fd)
{
    #if defined(LINENOISE_ISUNIX)
    /* Don't even check the return value as it's too late. */
    if(ctx->israwmode && tcsetattr(fd, TCSADRAIN, &ctx->origtermios) != -1)
    {
        ctx->israwmode = 0;
    }
    #endif
}

/* Use the ESC [6n escape sequence to query the horizontal cursor position
 * and return it. On error -1 is returned, on success the position of the
 * cursor. */
static int lino_util_getcursorposition(int ifd, int ofd)
{
    char buf[32];
    int cols;
    int rows;
    unsigned int i;
    i = 0;
    /* Report cursor location */
    if(write(ofd, "\x1b[6n", 4) != 4)
    {
        return -1;
    }
    /* Read the response: ESC [ rows ; cols R */
    while(i < sizeof(buf) - 1)
    {
        if(read(ifd, buf + i, 1) != 1)
        {
            break;
        }
        if(buf[i] == 'R')
        {
            break;
        }
        i++;
    }
    buf[i] = '\0';
    /* Parse it. */
    if(buf[0] != LINO_KEY_ESC || buf[1] != '[')
    {
        return -1;
    }
    if(sscanf(buf + 2, "%d;%d", &rows, &cols) != 2)
    {
        return -1;
    }
    return cols;
}

/* Try to get the number of columns in the current terminal, or assume 80
 * if it fails. */
static int lino_util_getcolumns(int ifd, int ofd)
{
    #if defined(LINENOISE_ISUNIX)
    int start;
    int cols;
    char seq[32];
    struct winsize ws;
    if(ioctl(1, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0)
    {
        /* ioctl() failed. Try to query the terminal itself. */
        /* Get the initial position so we can restore it later. */
        start = lino_util_getcursorposition(ifd, ofd);
        if(start == -1)
        {
            goto failed;
        }
        /* Go to right margin and get position. */
        if(write(ofd, "\x1b[999C", 6) != 6)
        {
            goto failed;
        }
        cols = lino_util_getcursorposition(ifd, ofd);
        if(cols == -1)
        {
            goto failed;
        }
        /* Restore position. */
        if(cols > start)
        {
            snprintf(seq, 32, "\x1b[%dD", cols - start);
            if(write(ofd, seq, strlen(seq)) == -1)
            {
                /* Can't recover... */
            }
        }
        return cols;
    }
    else
    {
        return ws.ws_col;
    }
    #endif
failed:
    return 80;
}

/* Clear the screen. Used to handle ctrl+l */
static void lino_clearscreen(void)
{
    if(write(fileno(stdout), "\x1b[H\x1b[2J", 7) <= 0)
    {
        /* nothing to do, just to avoid warning. */
    }
}

/* Beep, used for completion when there is nothing to complete or when all
 * the choices were already shown. */
static void lino_util_beep(void)
{
    fprintf(stderr, "\x7");
    fflush(stderr);
}

/* ============================== Completion ================================ */

/* Free a list of completion option populated by lino_addcompletion(). */
static void lino_completions_destroy(linocompletions_t* lc)
{
    size_t i;
    for(i = 0; i < lc->count; i++)
    {
        free(lc->cvec[i]);
    }
    if(lc->cvec != NULL)
    {
        free(lc->cvec);
    }
}

/* This is an helper function for lino_edit_editline() and is called when the
 * user types the <tab> key in order to complete the string currently in the
 * input.
 *
 * The state of the editing is encapsulated into the pointed linoeditstate_t
 * structure as described in the structure definition. */
static int lino_completeline(linocontext_t* ctx, linoeditstate_t* ls, char* cbuf, size_t cbuflen, int* c)
{
    int nread;
    int nwritten;
    size_t i;
    size_t stop;
    linoeditstate_t saved;
    linocompletions_t lc = { 0, NULL };
    nread = 0;
    *c = 0;
    ctx->completioncallback(ctx, ls->edlinebuf, &lc);
    if(lc.count == 0)
    {
        lino_util_beep();
    }
    else
    {
        stop = 0;
        i = 0;
        while(!stop)
        {
            /* Show completion or original buffer */
            if(i < lc.count)
            {
                saved = *ls;
                ls->currentedlinelen = ls->currentcursorpos = strlen(lc.cvec[i]);
                ls->edlinebuf = lc.cvec[i];
                lino_context_refreshline(ctx, ls);
                ls->currentedlinelen = saved.currentedlinelen;
                ls->currentcursorpos = saved.currentcursorpos;
                ls->edlinebuf = saved.edlinebuf;
            }
            else
            {
                lino_context_refreshline(ctx, ls);
            }
            nread = ctx->fnreadcode(ctx, ls->termstdinfd, cbuf, cbuflen, c);
            if(nread <= 0)
            {
                lino_completions_destroy(&lc);
                *c = -1;
                return nread;
            }
            switch(*c)
            {
                case 9: /* tab */
                    {
                        i = (i + 1) % (lc.count + 1);
                        if(i == lc.count)
                        {
                            lino_util_beep();
                        }
                    }
                    break;
                case 27: /* escape */
                    {
                        /* Re-show original buffer */
                        if(i < lc.count)
                        {
                            lino_context_refreshline(ctx, ls);
                        }
                        stop = 1;
                    }
                    break;
                default:
                    /* Update buffer and return */
                    {
                        if(i < lc.count)
                        {
                            nwritten = snprintf(ls->edlinebuf, ls->edlinelen, "%s", lc.cvec[i]);
                            ls->currentedlinelen = ls->currentcursorpos = nwritten;
                        }
                        stop = 1;
                    }
                    break;
            }
        }
    }
    lino_completions_destroy(&lc);
    return nread;
}

/* Register a callback function to be called for tab-completion. */
static void lino_setcompletioncallback(linocontext_t* ctx, linofncomp_t* fn)
{
    ctx->completioncallback = fn;
}

/* Register a hits function to be called to show hits to the user at the
 * right of the prompt. */
static void lino_sethintscallback(linocontext_t* ctx, linofnhint_t* fn)
{
    ctx->hintscallback = fn;
}

/* Register a function to free the hints returned by the hints callback
 * registered with lino_sethintscallback(). */
static void lino_setfreehintscallback(linocontext_t* ctx, linofnhintfree_t* fn)
{
    ctx->freehintscallback = fn;
}

/* This function is used by the callback function registered by the user
 * in order to add completion options given the input string when the
 * user typed <tab>. See the example.c source code for a very easy to
 * understand example. */
static void lino_addcompletion(linocontext_t* ctx, linocompletions_t* lc, const char* str)
{
    size_t len;
    char *copy;
    char** cvec;
    (void)ctx;
    len = strlen(str);
    copy = (char*)malloc(len + 1);
    if(copy == NULL)
    {
        return;
    }
    memcpy(copy, str, len + 1);
    cvec = (char**)realloc(lc->cvec, sizeof(char*) * (lc->count + 1));
    if(cvec == NULL)
    {
        free(copy);
        return;
    }
    lc->cvec = cvec;
    lc->cvec[lc->count++] = copy;
}

/* =========================== Line editing ================================= */

/* We define a very simple "append buffer" structure, that is an heap
 * allocated string where we can append to. This is useful in order to
 * write all the escape sequences in a buffer and flush them to the standard
 * output in a single call, to avoid flickering effects. */

static void lino_appendbuf_init(linobuffer_t* ab)
{
    ab->bufdata = NULL;
    ab->buflen = 0;
}

static void lino_appendbuf_append(linobuffer_t* ab, const char* s, int len)
{
    char* newbuf;
    newbuf = (char*)realloc(ab->bufdata, ab->buflen + len);
    if(newbuf == NULL)
    {
        return;
    }
    memcpy(newbuf + ab->buflen, s, len);
    ab->bufdata = newbuf;
    ab->buflen += len;
}

static void lino_appendbuf_destroy(linobuffer_t* ab)
{
    free(ab->bufdata);
}

/* Helper of lino_context_refreshsingleline() and lino_context_refreshmultiline() to show hints
 * to the right of the prompt. */
static void lino_refreshshowhints(linocontext_t* ctx, linobuffer_t* ab, linoeditstate_t* edst, int pcollen)
{
    int bold;
    int color;
    int hintlen;
    int hintmaxlen;
    size_t collen;
    char* hint;
    char seq[64];
    collen = pcollen + lino_context_columnpos(ctx, edst->edlinebuf, edst->currentedlinelen, edst->currentedlinelen);
    if(ctx->hintscallback && collen < edst->terminalcolumns)
    {
        color = -1;
        bold = 0;
        hint = ctx->hintscallback(ctx, edst->edlinebuf, &color, &bold);
        if(hint)
        {
            hintlen = strlen(hint);
            hintmaxlen = edst->terminalcolumns - collen;
            if(hintlen > hintmaxlen)
            {
                hintlen = hintmaxlen;
            }
            if(bold == 1 && color == -1)
            {
                color = 37;
            }
            if(color != -1 || bold != 0)
            {
                snprintf(seq, 64, "\033[%d;%d;49m", bold, color);
            }
            else
            {
                seq[0] = '\0';
            }
            lino_appendbuf_append(ab, seq, strlen(seq));
            lino_appendbuf_append(ab, hint, hintlen);
            if(color != -1 || bold != 0)
            {
                lino_appendbuf_append(ab, "\033[0m", 4);
            }
            /* Call the function to free the hint returned. */
            if(ctx->freehintscallback)
            {
                ctx->freehintscallback(ctx, hint);
            }
        }
    }
}

/* Check if text is an ANSI escape sequence
 */
static int lino_util_isansiescape(const char* buf, size_t buflen, size_t* len)
{
    size_t off;
    if(buflen > 2 && !memcmp("\033[", buf, 2))
    {
        off = 2;
        while(off < buflen)
        {
            switch(buf[off++])
            {
                case 'A':
                case 'B':
                case 'C':
                case 'D':
                case 'E':
                case 'F':
                case 'G':
                case 'H':
                case 'J':
                case 'K':
                case 'S':
                case 'T':
                case 'f':
                case 'm':
                    {
                        *len = off;
                        return 1;
                    }
                    break;
            }
        }
    }
    return 0;
}

/* Get column length of prompt text
 */
static size_t lino_util_prompttextcolumnlen(linocontext_t* ctx, const char* prompt, size_t plen)
{
    size_t off;
    size_t len;
    size_t buflen;
    char buf[LINENOISE_MAX_LINE];
    buflen = 0;
    off = 0;
    while(off < plen)
    {
        if(lino_util_isansiescape(prompt + off, plen - off, &len))
        {
            off += len;
            continue;
        }
        buf[buflen++] = prompt[off++];
    }
    return lino_context_columnpos(ctx, buf, buflen, buflen);
}

/* Single line low level line refresh.
 *
 * Rewrite the currently edited line accordingly to the buffer content,
 * cursor position, and number of columns of the terminal. */
static void lino_context_refreshsingleline(linocontext_t* ctx, linoeditstate_t* edst)
{
    int fd;
    int chlen;
    size_t pcollen;
    size_t len;
    size_t pos;
    char* buf;
    char seq[64];
    linobuffer_t ab;
    pcollen = lino_util_prompttextcolumnlen(ctx, edst->promptdata, strlen(edst->promptdata));
    fd = edst->termstdoutfd;
    buf = edst->edlinebuf;
    len = edst->currentedlinelen;
    pos = edst->currentcursorpos;
    while((pcollen + lino_context_columnpos(ctx, buf, len, pos)) >= edst->terminalcolumns)
    {
        chlen = ctx->fnnextcharlen(ctx, buf, len, 0, NULL);
        buf += chlen;
        len -= chlen;
        pos -= chlen;
    }
    while(pcollen + lino_context_columnpos(ctx, buf, len, len) > edst->terminalcolumns)
    {
        len -= ctx->fnprevcharlen(ctx, buf, len, len, NULL);
    }
    lino_appendbuf_init(&ab);
    /* Cursor to left edge */
    snprintf(seq, 64, "\r");
    lino_appendbuf_append(&ab, seq, strlen(seq));
    /* Write the prompt and the current buffer content */
    lino_appendbuf_append(&ab, edst->promptdata, strlen(edst->promptdata));
    if(ctx->maskmode == 1)
    {
        while(len--)
        {
            lino_appendbuf_append(&ab, "*", 1);
        }
    }
    else
    {
        lino_appendbuf_append(&ab, buf, len);
    }
    /* Show hits if any. */
    lino_refreshshowhints(ctx, &ab, edst, pcollen);
    /* Erase to right */
    snprintf(seq, 64, "\x1b[0K");
    lino_appendbuf_append(&ab, seq, strlen(seq));
    /* Move cursor to original position. */
    snprintf(seq, 64, "\r\x1b[%dC", (int)(lino_context_columnpos(ctx, buf, len, pos) + pcollen));
    lino_appendbuf_append(&ab, seq, strlen(seq));
    if(write(fd, ab.bufdata, ab.buflen) == -1)
    {
    } /* Can't recover from write error. */
    lino_appendbuf_destroy(&ab);
}

/* Multi line low level line refresh.
 *
 * Rewrite the currently edited line accordingly to the buffer content,
 * cursor position, and number of columns of the terminal. */
static void lino_context_refreshmultiline(linocontext_t* ctx, linoeditstate_t* edst)
{
    int j;
    int fd;
    int col; /* colum position, zero-based. */
    int rpos;
    int rpos2; /* rpos after refresh. */
    int rows;
    int colpos;
    int colpos2; /* cursor column position. */
    int oldrows;
    size_t pcollen;
    unsigned int i;
    char seq[64];
    linobuffer_t ab;
    pcollen = lino_util_prompttextcolumnlen(ctx, edst->promptdata, strlen(edst->promptdata));
    colpos = lino_context_columnposformultiline(ctx, edst->edlinebuf, edst->currentedlinelen, edst->currentedlinelen, edst->terminalcolumns, pcollen);
    rows = (pcollen + colpos + edst->terminalcolumns - 1) / edst->terminalcolumns; /* rows used by current buf. */
    rpos = (pcollen + edst->prevrefreshcursorpos + edst->terminalcolumns) / edst->terminalcolumns; /* cursor relative row. */
    oldrows = edst->maxrowsused;
    fd = edst->termstdoutfd;
    /* Update maxrowsused if needed. */
    if(rows > (int)edst->maxrowsused)
    {
        edst->maxrowsused = rows;
    }
    /* First step: clear all the lines used before. To do so start by
   * going to the last row. */
    lino_appendbuf_init(&ab);
    if(oldrows - rpos > 0)
    {
        lndebug("go down %d", oldrows - rpos);
        snprintf(seq, 64, "\x1b[%dB", oldrows - rpos);
        lino_appendbuf_append(&ab, seq, strlen(seq));
    }
    /* Now for every row clear it, go up. */
    for(j = 0; j < oldrows - 1; j++)
    {
        lndebug("clear+up");
        snprintf(seq, 64, "\r\x1b[0K\x1b[1A");
        lino_appendbuf_append(&ab, seq, strlen(seq));
    }
    /* Clean the top line. */
    lndebug("clear");
    snprintf(seq, 64, "\r\x1b[0K");
    lino_appendbuf_append(&ab, seq, strlen(seq));
    /* Write the prompt and the current buffer content */
    lino_appendbuf_append(&ab, edst->promptdata, strlen(edst->promptdata));
    if(ctx->maskmode == 1)
    {
        for(i = 0; i < edst->currentedlinelen; i++)
        {
            lino_appendbuf_append(&ab, "*", 1);
        }
    }
    else
    {
        lino_appendbuf_append(&ab, edst->edlinebuf, edst->currentedlinelen);
    }
    /* Show hits if any. */
    lino_refreshshowhints(ctx, &ab, edst, pcollen);
    /* Get column length to cursor position */
    colpos2 = lino_context_columnposformultiline(ctx, edst->edlinebuf, edst->currentedlinelen, edst->currentcursorpos, edst->terminalcolumns, pcollen);
    /* If we are at the very end of the screen with our prompt, we need to
   * emit a newline and move the prompt to the first column. */
    if(edst->currentcursorpos && edst->currentcursorpos == edst->currentedlinelen && (colpos2 + pcollen) % edst->terminalcolumns == 0)
    {
        lndebug("<newline>");
        lino_appendbuf_append(&ab, "\n", 1);
        snprintf(seq, 64, "\r");
        lino_appendbuf_append(&ab, seq, strlen(seq));
        rows++;
        if(rows > (int)edst->maxrowsused)
        {
            edst->maxrowsused = rows;
        }
    }
    /* Move cursor to right position. */
    rpos2 = (pcollen + colpos2 + edst->terminalcolumns) / edst->terminalcolumns; /* current cursor relative row. */
    lndebug("rpos2 %d", rpos2);
    /* Go up till we reach the expected positon. */
    if(rows - rpos2 > 0)
    {
        lndebug("go-up %d", rows - rpos2);
        snprintf(seq, 64, "\x1b[%dA", rows - rpos2);
        lino_appendbuf_append(&ab, seq, strlen(seq));
    }
    /* Set column. */
    col = (pcollen + colpos2) % edst->terminalcolumns;
    lndebug("set col %d", 1 + col);
    if(col)
    {
        snprintf(seq, 64, "\r\x1b[%dC", col);
    }
    else
    {
        snprintf(seq, 64, "\r");
    }
    lino_appendbuf_append(&ab, seq, strlen(seq));
    lndebug("\n");
    edst->prevrefreshcursorpos = colpos2;
    if(write(fd, ab.bufdata, ab.buflen) == -1)
    {
    } /* Can't recover from write error. */
    lino_appendbuf_destroy(&ab);
}

/* Calls the two low level functions lino_context_refreshsingleline() or
 * lino_context_refreshmultiline() according to the selected mode. */
static void lino_context_refreshline(linocontext_t* ctx, linoeditstate_t* edst)
{
    if(ctx->ismultilinemode)
    {
        lino_context_refreshmultiline(ctx, edst);
    }
    else
    {
        lino_context_refreshsingleline(ctx, edst);
    }
}

/* Insert the character 'c' at cursor current position.
 *
 * On error writing to the terminal -1 is returned, otherwise 0. */
static int lino_edit_insert(linocontext_t* ctx, linoeditstate_t* edst, const char* cbuf, int clen)
{
    if(edst->currentedlinelen + clen <= edst->edlinelen)
    {
        if(edst->currentedlinelen == edst->currentcursorpos)
        {
            memcpy(&edst->edlinebuf[edst->currentcursorpos], cbuf, clen);
            edst->currentcursorpos += clen;
            edst->currentedlinelen += clen;
            edst->edlinebuf[edst->currentedlinelen] = '\0';
            if((!ctx->ismultilinemode && lino_util_prompttextcolumnlen(ctx, edst->promptdata, edst->promptlen) + lino_context_columnpos(ctx, edst->edlinebuf, edst->currentedlinelen, edst->currentedlinelen) < edst->terminalcolumns && !ctx->hintscallback))
            {
                /* Avoid a full update of the line in the trivial case. */
                if(ctx->maskmode == 1)
                {
                    static const char d = '*';
                    if(write(edst->termstdoutfd, &d, 1) == -1)
                    {
                        return -1;
                    }
                }
                else
                {
                    if(write(edst->termstdoutfd, cbuf, clen) == -1)
                    {
                        return -1;
                    }
                }
            }
            else
            {
                lino_context_refreshline(ctx, edst);
            }
        }
        else
        {
            memmove(edst->edlinebuf + edst->currentcursorpos + clen, edst->edlinebuf + edst->currentcursorpos, edst->currentedlinelen - edst->currentcursorpos);
            memcpy(&edst->edlinebuf[edst->currentcursorpos], cbuf, clen);
            edst->currentcursorpos += clen;
            edst->currentedlinelen += clen;
            edst->edlinebuf[edst->currentedlinelen] = '\0';
            lino_context_refreshline(ctx, edst);
        }
    }
    return 0;
}

/* Move cursor on the left. */
static void lino_edit_moveleft(linocontext_t* ctx, linoeditstate_t* edst)
{
    if(edst->currentcursorpos > 0)
    {
        edst->currentcursorpos -= ctx->fnprevcharlen(ctx, edst->edlinebuf, edst->currentedlinelen, edst->currentcursorpos, NULL);
        lino_context_refreshline(ctx, edst);
    }
}

/* Move cursor on the right. */
static void lino_edit_moveright(linocontext_t* ctx, linoeditstate_t* edst)
{
    if(edst->currentcursorpos != edst->currentedlinelen)
    {
        edst->currentcursorpos += ctx->fnnextcharlen(ctx, edst->edlinebuf, edst->currentedlinelen, edst->currentcursorpos, NULL);
        lino_context_refreshline(ctx, edst);
    }
}

/* Move cursor to the start of the line. */
static void lino_edit_movehome(linocontext_t* ctx, linoeditstate_t* edst)
{
    if(edst->currentcursorpos != 0)
    {
        edst->currentcursorpos = 0;
        lino_context_refreshline(ctx, edst);
    }
}

/* Move cursor to the end of the line. */
static void lino_edit_moveend(linocontext_t* ctx, linoeditstate_t* edst)
{
    if(edst->currentcursorpos != edst->currentedlinelen)
    {
        edst->currentcursorpos = edst->currentedlinelen;
        lino_context_refreshline(ctx, edst);
    }
}

/* Substitute the currently edited line with the next or previous history
 * entry as specified by 'dir'. */
#define LINENOISE_HISTORY_NEXT 0
#define LINENOISE_HISTORY_PREV 1

static void lino_edit_historynext(linocontext_t* ctx, linoeditstate_t* edst, int dir)
{
    int ipos;
    if(ctx->historylength > 1)
    {
        /* Update the current history entry before to overwrite it with the next one. */
        ipos = ctx->historylength - 1 - edst->historyindex;
        free(ctx->historybuflines[ipos]);
        ctx->historybuflines[ipos] = strdup(edst->edlinebuf);
        /* Show the new entry */
        edst->historyindex += (dir == LINENOISE_HISTORY_PREV) ? 1 : -1;
        if(edst->historyindex < 0)
        {
            edst->historyindex = 0;
            return;
        }
        else if(edst->historyindex >= ctx->historylength)
        {
            edst->historyindex = ctx->historylength - 1;
            return;
        }
        strncpy(edst->edlinebuf, ctx->historybuflines[ctx->historylength - 1 - edst->historyindex], edst->edlinelen);
        edst->edlinebuf[edst->edlinelen - 1] = '\0';
        edst->currentedlinelen = edst->currentcursorpos = strlen(edst->edlinebuf);
        lino_context_refreshline(ctx, edst);
    }
}

/* Delete the character at the right of the cursor without altering the cursor
 * position. Basically this is what happens with the "Delete" keyboard key. */
static void lino_edit_delete(linocontext_t* ctx, linoeditstate_t* edst)
{
    int chlen;
    if(edst->currentedlinelen > 0 && edst->currentcursorpos < edst->currentedlinelen)
    {
        chlen = ctx->fnnextcharlen(ctx, edst->edlinebuf, edst->currentedlinelen, edst->currentcursorpos, NULL);
        memmove(edst->edlinebuf + edst->currentcursorpos, edst->edlinebuf + edst->currentcursorpos + chlen, edst->currentedlinelen - edst->currentcursorpos - chlen);
        edst->currentedlinelen -= chlen;
        edst->edlinebuf[edst->currentedlinelen] = '\0';
        lino_context_refreshline(ctx, edst);
    }
}

/* Backspace implementation. */
static void lino_edit_backspace(linocontext_t* ctx, linoeditstate_t* edst)
{
    int chlen;
    if(edst->currentcursorpos > 0 && edst->currentedlinelen > 0)
    {
        chlen = ctx->fnprevcharlen(ctx, edst->edlinebuf, edst->currentedlinelen, edst->currentcursorpos, NULL);
        memmove(edst->edlinebuf + edst->currentcursorpos - chlen, edst->edlinebuf + edst->currentcursorpos, edst->currentedlinelen - edst->currentcursorpos);
        edst->currentcursorpos -= chlen;
        edst->currentedlinelen -= chlen;
        edst->edlinebuf[edst->currentedlinelen] = '\0';
        lino_context_refreshline(ctx, edst);
    }
}

/* Delete the previosu word, maintaining the cursor at the start of the
 * current word. */
static void lino_edit_delprevword(linocontext_t* ctx, linoeditstate_t* edst)
{
    size_t diff;
    size_t oldpos;
    oldpos = edst->currentcursorpos;
    while(edst->currentcursorpos > 0 && edst->edlinebuf[edst->currentcursorpos - 1] == ' ')
    {
        edst->currentcursorpos--;
    }
    while(edst->currentcursorpos > 0 && edst->edlinebuf[edst->currentcursorpos - 1] != ' ')
    {
        edst->currentcursorpos--;
    }
    diff = oldpos - edst->currentcursorpos;
    memmove(edst->edlinebuf + edst->currentcursorpos, edst->edlinebuf + oldpos, edst->currentedlinelen - oldpos + 1);
    edst->currentedlinelen -= diff;
    lino_context_refreshline(ctx, edst);
}

/* This function is the core of the line editing capability of linenoise.
 * It expects 'fd' to be already in "raw mode" so that every key pressed
 * will be returned ASAP to read().
 *
 * The resulting string is put into 'buf' when the user type enter, or
 * when ctrl+d is typed.
 *
 * The function returns the length of the current buffer. */
static int lino_edit_editline(linocontext_t* ctx, int stdin_fd, int stdout_fd, char* buf, size_t buflen, const char* prompt)
{
    int c;
    int aux;
    int nread;
    char seq[3];
    char cbuf[32];/* large enough for any encoding? */
    linoeditstate_t edst;
    /* Populate the linenoise state that we pass to functions implementing
   * specific editing functionalities. */
    edst.termstdinfd = stdin_fd;
    edst.termstdoutfd = stdout_fd;
    edst.edlinebuf = buf;
    edst.edlinelen = buflen;
    edst.promptdata = prompt;
    edst.promptlen = strlen(prompt);
    edst.prevrefreshcursorpos = edst.currentcursorpos = 0;
    edst.currentedlinelen = 0;
    edst.terminalcolumns = lino_util_getcolumns(stdin_fd, stdout_fd);
    edst.maxrowsused = 0;
    edst.historyindex = 0;
    /* Buffer starts empty. */
    edst.edlinebuf[0] = '\0';
    edst.edlinelen--; /* Make sure there is always space for the nulterm */
    /* The latest history entry is always our current buffer, that initially is just an empty string. */
    lino_context_historyadd(ctx, "");
    if(write(edst.termstdoutfd, prompt, edst.promptlen) == -1)
    {
        return -1;
    }
    while(1)
    {
        nread = ctx->fnreadcode(ctx, edst.termstdinfd, cbuf, sizeof(cbuf), &c);
        if(nread <= 0)
        {
            return edst.currentedlinelen;
        }
        /*
        * Only autocomplete when the callback is set. It returns < 0 when
        * there was an error reading from fd. Otherwise it will return the
        * character that should be handled next.
        */
        if(c == 9 && ctx->completioncallback != NULL)
        {
            nread = lino_completeline(ctx, &edst, cbuf, sizeof(cbuf), &c);
            /* Return on errors */
            if(c < 0)
            {
                return edst.currentedlinelen;
            }
            /* Read next character when 0 */
            if(c == 0)
            {
                continue;
            }
        }
        switch(c)
        {
            case LINO_KEY_LINEFEED:
            case LINO_KEY_ENTER: /* enter */
                {
                    linofnhint_t* hc;
                    ctx->historylength--;
                    free(ctx->historybuflines[ctx->historylength]);
                    ctx->historybuflines[ctx->historylength] = NULL;
                    if(ctx->ismultilinemode)
                    {
                        lino_edit_moveend(ctx, &edst);
                    }
                    if(ctx->hintscallback)
                    {
                        /* Force a refresh without hints to leave the previous line as the user typed it after a newline. */
                        hc = ctx->hintscallback;
                        ctx->hintscallback = NULL;
                        lino_context_refreshline(ctx, &edst);
                        ctx->hintscallback = hc;
                    }
                    return (int)edst.currentedlinelen;
                }
                break;
            case LINO_KEY_CTRLC: /* ctrl-c, clear line */
                {
                    buf[0] = '\0';
                    edst.currentcursorpos = edst.currentedlinelen = 0;
                    lino_context_refreshline(ctx, &edst);
                }
                break;
            case LINO_KEY_BACKSPACE: /* backspace */
            case 8: /* ctrl-h */
                {
                    lino_edit_backspace(ctx, &edst);
                }
                break;
            case LINO_KEY_CTRLD: /* ctrl-d, act as end-of-file. */
                {
                    ctx->historylength--;
                    free(ctx->historybuflines[ctx->historylength]);
                    ctx->historybuflines[ctx->historylength] = NULL;
                    return -1;
                }
                break;
            case LINO_KEY_CTRLT: /* ctrl-t, swaps current character with previous. */
                {
                    if(edst.currentcursorpos > 0 && edst.currentcursorpos < edst.currentedlinelen)
                    {
                        aux = buf[edst.currentcursorpos - 1];
                        buf[edst.currentcursorpos - 1] = buf[edst.currentcursorpos];
                        buf[edst.currentcursorpos] = aux;
                        if(edst.currentcursorpos != edst.currentedlinelen - 1)
                        {
                            edst.currentcursorpos++;
                        }
                        lino_context_refreshline(ctx, &edst);
                    }
                }
                break;
            case LINO_KEY_CTRLB: /* ctrl-b */
                {
                    lino_edit_moveleft(ctx, &edst);
                }
                break;
            case LINO_KEY_CTRLF: /* ctrl-f */
                {
                    lino_edit_moveright(ctx, &edst);
                }
                break;
            case LINO_KEY_CTRLP: /* ctrl-p */
                {
                    lino_edit_historynext(ctx, &edst, LINENOISE_HISTORY_PREV);
                }
                break;
            case LINO_KEY_CTRLN: /* ctrl-n */
                {
                    lino_edit_historynext(ctx, &edst, LINENOISE_HISTORY_NEXT);
                }
                break;
            case LINO_KEY_ESC: /* escape sequence */
                {
                    /*
                    * Read the next two bytes representing the escape sequence.
                    * Use two calls to handle slow terminals returning the two
                    * chars at different times.
                    */
                    if(read(edst.termstdinfd, seq, 1) == -1)
                    {
                        break;
                    }
                    if(read(edst.termstdinfd, seq + 1, 1) == -1)
                    {
                        break;
                    }
                    /* ESC [ sequences. */
                    if(seq[0] == '[')
                    {
                        if(seq[1] >= '0' && seq[1] <= '9')
                        {
                            /* Extended escape, read additional byte. */
                            if(read(edst.termstdinfd, seq + 2, 1) == -1)
                            {
                                break;
                            }
                            if(seq[2] == '~')
                            {
                                switch(seq[1])
                                {
                                    case '3': /* Delete key. */
                                        {
                                            lino_edit_delete(ctx, &edst);
                                        }
                                        break;
                                }
                            }
                        }
                        else
                        {
                            switch(seq[1])
                            {
                                case 'A': /* Up */
                                    {
                                        lino_edit_historynext(ctx, &edst, LINENOISE_HISTORY_PREV);
                                    }
                                    break;
                                case 'B': /* Down */
                                    {
                                        lino_edit_historynext(ctx, &edst, LINENOISE_HISTORY_NEXT);
                                    }
                                    break;
                                case 'C': /* Right */
                                    {
                                        lino_edit_moveright(ctx, &edst);
                                    }
                                    break;
                                case 'D': /* Left */
                                    {
                                        lino_edit_moveleft(ctx, &edst);
                                    }
                                    break;
                                case 'H': /* Home */
                                    {
                                        lino_edit_movehome(ctx, &edst);
                                    }
                                    break;
                                case 'F': /* End*/
                                    {
                                        lino_edit_moveend(ctx, &edst);
                                    }
                                    break;
                            }
                        }
                    }
                    /* ESC O sequences. */
                    else if(seq[0] == 'O')
                    {
                        switch(seq[1])
                        {
                            case 'H': /* Home */
                                {
                                    lino_edit_movehome(ctx, &edst);
                                }
                                break;
                            case 'F': /* End*/
                                {
                                    lino_edit_moveend(ctx, &edst);
                                }
                                break;
                        }
                    }
                }
                break;
            default:
                {
                    if(lino_edit_insert(ctx, &edst, cbuf, nread))
                    {
                        return -1;
                    }
                }
                break;
            case LINO_KEY_CTRLU: /* Ctrl+u, delete the whole line. */
                {
                    buf[0] = '\0';
                    edst.currentcursorpos = edst.currentedlinelen = 0;
                    lino_context_refreshline(ctx, &edst);
                }
                break;
            case LINO_KEY_CTRLK: /* Ctrl+k, delete from current to end of line. */
                {
                    buf[edst.currentcursorpos] = '\0';
                    edst.currentedlinelen = edst.currentcursorpos;
                    lino_context_refreshline(ctx, &edst);
                }
                break;
            case LINO_KEY_CTRLA: /* Ctrl+a, go to the start of the line */
                {
                    lino_edit_movehome(ctx, &edst);
                }
                break;
            case LINO_KEY_CTRLE: /* ctrl+e, go to the end of the line */
                {
                    lino_edit_moveend(ctx, &edst);
                }
                break;
            case LINO_KEY_CTRLL: /* ctrl+edst, clear screen */
                {
                    lino_clearscreen();
                    lino_context_refreshline(ctx, &edst);
                }
                break;
            case LINO_KEY_CTRLW: /* ctrl+w, delete previous word */
                {
                    lino_edit_delprevword(ctx, &edst);
                }
                break;
        }
    }
    return edst.currentedlinelen;
}

/* This special mode is used by linenoise in order to print scan codes
 * on screen for debugging / development purposes. It is implemented
 * by the linenoise_example program using the --keycodes option. */
static void lino_debug_printkeycodes(linocontext_t* ctx)
{
    int nread;
    char c;
    char quit[4];

    printf(
        "Linenoise key codes debugging mode.\n"
        "Press keys to see scan codes. Type 'quit' at any time to exit.\n"
    );
    if(lino_context_enablerawmode(ctx, fileno(stdin)) == -1)
    {
        return;
    }
    memset(quit, ' ', 4);
    while(1)
    {
        nread = read(fileno(stdin), &c, 1);
        if(nread <= 0)
        {
            continue;
        }
        memmove(quit, quit + 1, sizeof(quit) - 1); /* shift string to left. */
        quit[sizeof(quit) - 1] = c; /* Insert current char on the right. */
        if(memcmp(quit, "quit", sizeof(quit)) == 0)
        {
            break;
        }
        printf("'%c' %02x (%d) (type quit to exit)\n", isprint((int)c) ? c : '?', (int)c, (int)c);
        printf("\r"); /* Go left edge manually, we are in raw mode. */
        fflush(stdout);
    }
    lino_context_disablerawmode(ctx, fileno(stdin));
}

/* This function calls the line editing function lino_edit_editline() using
 * the STDIN file descriptor set in raw mode. */
static int lino_context_getraw(linocontext_t* ctx, char* buf, size_t buflen, const char* prompt)
{
    int count;
    if(buflen == 0)
    {
        errno = EINVAL;
        return -1;
    }
    #if defined(LINENOISE_ISUNIX)
    if(lino_context_enablerawmode(ctx, fileno(stdin)) == -1)
    {
        return -1;
    }
    #endif
    count = lino_edit_editline(ctx, fileno(stdin), fileno(stdout), buf, buflen, prompt);
    lino_context_disablerawmode(ctx, fileno(stdin));
    printf("\n");
    return count;
}

/* This function is called when linenoise() is called with the standard
 * input file descriptor not attached to a TTY. So for example when the
 * program using linenoise is called in pipe or with a file redirected
 * to its standard input. In this case, we want to be able to return the
 * line regardless of its length (by default we are limited to 4k). */
static char* lino_context_fallbacknotty(linocontext_t* ctx)
{
    int ch;
    size_t len;
    size_t maxlen;
    char* line;
    char* oldval;
    (void)ctx;
    line = NULL;
    len = 0;
    maxlen = 0;
    while(1)
    {
        if(len == maxlen)
        {
            if(maxlen == 0)
            {
                maxlen = 16;
            }
            maxlen *= 2;
            oldval = line;
            line = (char*)realloc(line, maxlen);
            if(line == NULL)
            {
                if(oldval)
                {
                    free(oldval);
                }
                return NULL;
            }
        }
        ch = fgetc(stdin);
        if(ch == EOF || ch == '\n')
        {
            if(ch == EOF && len == 0)
            {
                free(line);
                return NULL;
            }
            else
            {
                line[len] = '\0';
                return line;
            }
        }
        else
        {
            line[len] = ch;
            len++;
        }
    }
    return NULL;
}

/* The high level function that is the main API of the linenoise library.
 * This function checks if the terminal has basic capabilities, just checking
 * for a blacklist of stupid terminals, and later either calls the line
 * editing function or uses dummy fgets() so that you will be able to type
 * something even in the most desperate of the conditions. */
static char* lino_context_readline(linocontext_t* ctx, const char* prompt)
{
    size_t len;
    int count;
    char buf[LINENOISE_MAX_LINE];
    if(!isatty(fileno(stdin)))
    {
        #if defined(LINENOISE_ISUNIX)
        /*
        * Not a tty: read from file / pipe. In this mode we don't want any
        * limit to the line size, so we call a function to handle that.
        */
        return lino_context_fallbacknotty(ctx);
        #endif
    }
    else if(lino_util_isunsupportedterm())
    {
        printf("%s", prompt);
        fflush(stdout);
        if(fgets(buf, LINENOISE_MAX_LINE, stdin) == NULL)
        {
            return NULL;
        }
        len = strlen(buf);
        while(len && (buf[len - 1] == '\n' || buf[len - 1] == '\r'))
        {
            len--;
            buf[len] = '\0';
        }
        return strdup(buf);
    }
    count = lino_context_getraw(ctx, buf, LINENOISE_MAX_LINE, prompt);
    if(count == -1)
    {
        return NULL;
    }
    return strdup(buf);
}

/* This is just a wrapper the user may want to call in order to make sure
 * the linenoise returned buffer is freed with the same allocator it was
 * created with. Useful when the main program is using an alternative
 * allocator. */
static void lino_context_freeline(linocontext_t* ctx, void* ptr)
{
    (void)ctx;
    free(ptr);
}

/* ================================ History ================================= */

/* Free the history, but does not reset it. Only used when we have to
 * exit() to avoid memory leaks are reported by valgrind & co. */
static void lino_context_freehistory(linocontext_t* ctx)
{
    int j;
    if(ctx->historybuflines != NULL)
    {
        for(j = 0; j < ctx->historylength; j++)
        {
            if(ctx->historybuflines[j] != NULL)
            {
                free(ctx->historybuflines[j]);
                ctx->historybuflines[j] = NULL;
            }
        }
        free(ctx->historybuflines);
        ctx->historybuflines = NULL;
    }
}

/* At exit we'll try to fix the terminal to the initial conditions. */
static void lino_context_handleimplicitexit()
{
    lino_context_disablerawmode(g_linoconst_gcontext, fileno(stdin));
    lino_context_freehistory(g_linoconst_gcontext);
}

/* This is the API call to add a new entry in the linenoise history.
 * It uses a fixed array of char pointers that are shifted (memmoved)
 * when the history max length is reached in order to remove the older
 * entry and make room for the new one, so it is not exactly suitable for huge
 * histories, but will work well for a few hundred of entries.
 *
 * Using a circular buffer is smarter, but a bit more complex to handle. */
static int lino_context_historyadd(linocontext_t* ctx, const char* line)
{
    char* linecopy;
    if(ctx->historymaxlen == 0)
    {
        return 0;
    }
    /* Initialization on first call. */
    if(ctx->historybuflines == NULL)
    {
        ctx->historybuflines = (char**)malloc(sizeof(char*) * ctx->historymaxlen);
        assert(ctx->historybuflines != NULL);
        if(ctx->historybuflines == NULL)
        {
            return 0;
        }
        memset(ctx->historybuflines, 0, (sizeof(char*) * ctx->historymaxlen));
    }
    /* Don't add duplicated lines. */
    if(ctx->historylength && !strcmp(ctx->historybuflines[ctx->historylength - 1], line))
    {
        return 0;
    }
    /* Add an heap allocated copy of the line in the history.
   * If we reached the max length, remove the older line. */
    linecopy = strdup(line);
    assert(linecopy != NULL);
    if(!linecopy)
    {
        return 0;
    }
    if(ctx->historylength == ctx->historymaxlen)
    {
        free(ctx->historybuflines[0]);
        ctx->historybuflines[0] = NULL;
        memmove(ctx->historybuflines, ctx->historybuflines + 1, sizeof(char*) * (ctx->historymaxlen - 1));
        ctx->historylength--;
    }
    ctx->historybuflines[ctx->historylength] = linecopy;
    ctx->historylength++;
    return 1;
}

/* Set the maximum length for the history. This function can be called even
 * if there is already some history, the function will make sure to retain
 * just the latest 'len' elements if the new history length value is smaller
 * than the amount of items already inside the history. */
static int lino_context_historysetmaxlength(linocontext_t* ctx, int len)
{
    int j;
    int tocopy;
    char** newbuf;
    if(len < 1)
    {
        return 0;
    }
    if(ctx->historybuflines)
    {
        tocopy = ctx->historylength;
        newbuf = (char**)malloc(sizeof(char*) * len);
        if(newbuf == NULL)
        {
            return 0;
        }
        /* If we can't copy everything, free the elements we'll not use. */
        if(len < tocopy)
        {
            for(j = 0; j < tocopy - len; j++)
            {
                free(ctx->historybuflines[j]);
                ctx->historybuflines[j] = NULL;
            }
            tocopy = len;
        }
        memset(newbuf, 0, sizeof(char*) * len);
        memcpy(newbuf, ctx->historybuflines + (ctx->historylength - tocopy), sizeof(char*) * tocopy);
        free(ctx->historybuflines);
        ctx->historybuflines = newbuf;
    }
    ctx->historymaxlen = len;
    if(ctx->historylength > ctx->historymaxlen)
    {
        ctx->historylength = ctx->historymaxlen;
    }
    return 1;
}

/* Save the history in the specified file. On success 0 is returned
 * otherwise -1 is returned. */
static int lino_context_historysavetofile(linocontext_t* ctx, const char* filename)
{
    #if defined(LINENOISE_ISUNIX)
    int j;
    mode_t old_umask;
    FILE* fp;
    old_umask = umask(S_IXUSR | S_IRWXG | S_IRWXO);
    fp = fopen(filename, "w");
    umask(old_umask);
    if(fp == NULL)
    {
        return -1;
    }
    chmod(filename, S_IRUSR | S_IWUSR);
    for(j = 0; j < ctx->historylength; j++)
    {
        fprintf(fp, "%s\n", ctx->historybuflines[j]);
    }
    fclose(fp);
    #endif
    return 0;
}

/* Load the history from the specified file. If the file does not exist
 * zero is returned and no operation is performed.
 *
 * If the file exists and the operation succeeded 0 is returned, otherwise
 * on error -1 is returned. */
static int lino_context_historyloadfromfile(linocontext_t* ctx, const char* filename)
{
    FILE* fp;
    char* p;
    char buf[LINENOISE_MAX_LINE];
    fp = fopen(filename, "r");
    if(fp == NULL)
    {
        return -1;
    }
    while(fgets(buf, LINENOISE_MAX_LINE, fp) != NULL)
    {
        p = strchr(buf, '\r');
        if(!p)
        {
            p = strchr(buf, '\n');
        }
        if(p)
        {
            *p = '\0';
        }
        lino_context_historyadd(ctx, buf);
    }
    fclose(fp);
    return 0;
}

#endif

