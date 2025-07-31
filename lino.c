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

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>

#if defined(__linux__) || defined(__unix__)
    #include <sys/ioctl.h>
    #include <termios.h>
    #include <unistd.h>
#endif

#include "lino.h"
#include "mem.h"

#define LINENOISE_DEFAULT_HISTORY_MAX_LEN 100
#define LINENOISE_MAX_LINE 4096
#define UNUSED(x) (void)(x)

#if defined(__linux__) || defined(__unix__)
    #define LINENOISE_ISUNIX
#endif


static const char* g_unsupportedterminals[] = { "dumb", "cons25", "emacs", NULL };
static linofncompletionfunc_t* g_completioncallback = NULL;
static linofnhintsfunc_t* g_hintscallback = NULL;
static linofnfreehintsfunc_t* g_freehintscallback = NULL;

#if defined(LINENOISE_ISUNIX)
static struct termios g_origtermios; /* In order to restore at exit.*/
#endif
static int g_maskmode = 0; /* Show "***" instead of input. For passwords. */
static int g_rawmode = 0; /* For atexit() function to check if restore is needed*/
static int g_mlmode = 0; /* Multi line mode. Default is single line. */
static int g_atexitregistered = 0; /* Register atexit just 1 time. */
static int g_historymaxlen = LINENOISE_DEFAULT_HISTORY_MAX_LEN;
static int g_historylength = 0;
static char** g_historybuflines = NULL;


/* Set default encoding functions */
static linofnprevcharlenfunc_t* g_fnprevcharlen = lino_util_defaultprevcharlen;
static linofnnextcharlenfunc_t* g_fnnextcharlen = lino_util_defaultnextcharlen;
static linofnreadcodefunc_t* g_fnreadcode = lino_util_defaultreadcode;


/* Debugging macro. */
#if 0
FILE *lndebug_fp = NULL;
    #define lndebug(...)                                                                                                                                    \
        do                                                                                                                                                  \
        {                                                                                                                                                   \
            if(lndebug_fp == NULL)                                                                                                                          \
            {                                                                                                                                               \
                lndebug_fp = fopen("/tmp/lndebug.txt", "a");                                                                                                \
                fprintf(lndebug_fp, "[%d %d %d] p: %d, rows: %d, rpos: %d, max: %d, oldmax: %d\n", (int)l->len, (int)l->pos, (int)l->oldcolpos, plen, rows, \
                        rpos, (int)l->maxrows, oldrows);                                                                                                   \
            }                                                                                                                                               \
            fprintf(lndebug_fp, ", " __VA_ARGS__);                                                                                                          \
            fflush(lndebug_fp);                                                                                                                             \
        } while(0)
#else
    #define lndebug(fmt, ...)
#endif


/* ========================== Encoding functions ============================= */

static const char g_strcmpcharmap[] = {
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

int lino_util_strcasecmp(const char* s1, const char* s2)
{
    const char *cm;
    cm = g_strcmpcharmap;
	while (cm[(int)*s1] == cm[(int)*s2++])
    {
		if (*s1++ == '\0')
        {
            return(0);
        }
	}
    return(cm[(int)*s1] - cm[(int)*--s2]);
}

int lino_util_strncasecmp(const char* s1, const char* s2, int n)
{
	const char *cm;
    cm = g_strcmpcharmap;
	while (--n >= 0 && cm[(int)*s1] == cm[(int)*s2++])
	{
        if (*s1++ == '\0')
        {
			return(0);
        }
    }
	if(n < 0)
    {
        return 0;
    }
    return (cm[(int)*s1] - cm[(int)*--s2]);
}


/* Get byte length and column length of the previous character */
size_t lino_util_defaultprevcharlen(const char* buf, size_t buflen, size_t pos, size_t* collen)
{
    UNUSED(buf);
    UNUSED(buflen);
    UNUSED(pos);
    if(collen != NULL)
    {
        *collen = 1;
    }
    return 1;
}

/* Get byte length and column length of the next character */
size_t lino_util_defaultnextcharlen(const char* buf, size_t buflen, size_t pos, size_t* collen)
{
    UNUSED(buf);
    UNUSED(buflen);
    UNUSED(pos);
    if(collen != NULL)
    {
        *collen = 1;
    }
    return 1;
}

/* Read bytes of the next character */
size_t lino_util_defaultreadcode(int fd, char* buf, size_t buflen, int* c)
{
    int nread;
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
void lino_setencodingfunctions(linofnprevcharlenfunc_t* prevCharLenFunc, linofnnextcharlenfunc_t* nextCharLenFunc, linofnreadcodefunc_t* readCodeFunc)
{
    g_fnprevcharlen = prevCharLenFunc;
    g_fnnextcharlen = nextCharLenFunc;
    g_fnreadcode = readCodeFunc;
}

/* Get column length from begining of buffer to current byte position */
size_t lino_util_columnpos(const char* buf, size_t buflen, size_t pos)
{
    size_t ret;
    size_t off;
    size_t len;
    size_t collen;
    ret = 0;
    off = 0;
    while(off < pos)
    {
        len = g_fnnextcharlen(buf, buflen, off, &collen);
        off += len;
        ret += collen;
    }
    return ret;
}

/* Get column length from begining of buffer to current byte position for multiline mode*/
size_t lino_util_columnposformultiline(const char* buf, size_t buflen, size_t pos, size_t cols, size_t ini_pos)
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
        len = g_fnnextcharlen(buf, buflen, off, &collen);
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
void lino_maskmodeenable(void)
{
    g_maskmode = 1;
}

/* Disable mask mode. */
void lino_maskmodedisable(void)
{
    g_maskmode = 0;
}

/* Set if to use or not the multi line mode. */
void lino_setmultiline(int ml)
{
    g_mlmode = ml;
}

/* Return true if the terminal name is in the list of terminals we know are
 * not able to understand basic escape sequences. */
int lino_util_isunsupportedterm(void)
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
    for(j = 0; g_unsupportedterminals[j]; j++)
    {
        if(!lino_util_strcasecmp(term, g_unsupportedterminals[j]))
        {
            return 1;
        }
    }
    return 0;
}

/* Raw mode: 1960 magic shit. */
int lino_util_enablerawmode(int fd)
{
    #if defined(LINENOISE_ISUNIX)
    struct termios raw;
    if(!isatty(fileno(stdin)))
    {
        goto fatal;
    }
    if(!g_atexitregistered)
    {
        atexit(lino_atexit);
        g_atexitregistered = 1;
    }
    if(tcgetattr(fd, &g_origtermios) == -1)
    {
        goto fatal;
    }
    raw = g_origtermios; /* modify the original mode */
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
    g_rawmode = 1;
    return 0;
fatal:
    errno = ENOTTY;
    #endif
    return -1;
}

void lino_util_disablerawmode(int fd)
{
    #if defined(LINENOISE_ISUNIX)
    /* Don't even check the return value as it's too late. */
    if(g_rawmode && tcsetattr(fd, TCSADRAIN, &g_origtermios) != -1)
    {
        g_rawmode = 0;
    }
    #endif
}

/* Use the ESC [6n escape sequence to query the horizontal cursor position
 * and return it. On error -1 is returned, on success the position of the
 * cursor. */
int lino_util_getcursorposition(int ifd, int ofd)
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
int lino_util_getcolumns(int ifd, int ofd)
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
void lino_clearscreen(void)
{
    if(write(fileno(stdout), "\x1b[H\x1b[2J", 7) <= 0)
    {
        /* nothing to do, just to avoid warning. */
    }
}

/* Beep, used for completion when there is nothing to complete or when all
 * the choices were already shown. */
void lino_util_beep(void)
{
    fprintf(stderr, "\x7");
    fflush(stderr);
}

/* ============================== Completion ================================ */

/* Free a list of completion option populated by lino_addcompletion(). */
void lino_freecompletions(linocompletions_t* lc)
{
    size_t i;
    for(i = 0; i < lc->len; i++)
    {
        nn_memory_free(lc->cvec[i]);
    }
    if(lc->cvec != NULL)
    {
        nn_memory_free(lc->cvec);
    }
}

/* This is an helper function for lino_editline() and is called when the
 * user types the <tab> key in order to complete the string currently in the
 * input.
 *
 * The state of the editing is encapsulated into the pointed linostate_t
 * structure as described in the structure definition. */
int lino_completeline(linostate_t* ls, char* cbuf, size_t cbuflen, int* c)
{
    int nread;
    int nwritten;
    size_t i;
    size_t stop;
    linostate_t saved;
    linocompletions_t lc = { 0, NULL };
    nread = 0;
    *c = 0;
    g_completioncallback(ls->buf, &lc);
    if(lc.len == 0)
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
            if(i < lc.len)
            {
                saved = *ls;
                ls->len = ls->pos = strlen(lc.cvec[i]);
                ls->buf = lc.cvec[i];
                lino_refreshline(ls);
                ls->len = saved.len;
                ls->pos = saved.pos;
                ls->buf = saved.buf;
            }
            else
            {
                lino_refreshline(ls);
            }
            nread = g_fnreadcode(ls->ifd, cbuf, cbuflen, c);
            if(nread <= 0)
            {
                lino_freecompletions(&lc);
                *c = -1;
                return nread;
            }
            switch(*c)
            {
                case 9: /* tab */
                    {
                        i = (i + 1) % (lc.len + 1);
                        if(i == lc.len)
                        {
                            lino_util_beep();
                        }
                    }
                    break;
                case 27: /* escape */
                    {
                        /* Re-show original buffer */
                        if(i < lc.len)
                        {
                            lino_refreshline(ls);
                        }
                        stop = 1;
                    }
                    break;
                default:
                    /* Update buffer and return */
                    {
                        if(i < lc.len)
                        {
                            nwritten = snprintf(ls->buf, ls->buflen, "%s", lc.cvec[i]);
                            ls->len = ls->pos = nwritten;
                        }
                        stop = 1;
                    }
                    break;
            }
        }
    }
    lino_freecompletions(&lc);
    return nread;
}

/* Register a callback function to be called for tab-completion. */
void lino_setcompletioncallback(linofncompletionfunc_t* fn)
{
    g_completioncallback = fn;
}

/* Register a hits function to be called to show hits to the user at the
 * right of the prompt. */
void lino_sethintscallback(linofnhintsfunc_t* fn)
{
    g_hintscallback = fn;
}

/* Register a function to free the hints returned by the hints callback
 * registered with lino_sethintscallback(). */
void lino_setfreehintscallback(linofnfreehintsfunc_t* fn)
{
    g_freehintscallback = fn;
}

/* This function is used by the callback function registered by the user
 * in order to add completion options given the input string when the
 * user typed <tab>. See the example.c source code for a very easy to
 * understand example. */
void lino_addcompletion(linocompletions_t* lc, const char* str)
{
    size_t len;
    char *copy;
    char** cvec;
    len = strlen(str);
    copy = (char*)nn_memory_malloc(len + 1);
    if(copy == NULL)
    {
        return;
    }
    memcpy(copy, str, len + 1);
    cvec = (char**)nn_memory_realloc(lc->cvec, sizeof(char*) * (lc->len + 1));
    if(cvec == NULL)
    {
        nn_memory_free(copy);
        return;
    }
    lc->cvec = cvec;
    lc->cvec[lc->len++] = copy;
}

/* =========================== Line editing ================================= */

/* We define a very simple "append buffer" structure, that is an heap
 * allocated string where we can append to. This is useful in order to
 * write all the escape sequences in a buffer and flush them to the standard
 * output in a single call, to avoid flickering effects. */

void lino_appendbuf_init(linobuffer_t* ab)
{
    ab->b = NULL;
    ab->len = 0;
}

void lino_appendbuf_append(linobuffer_t* ab, const char* s, int len)
{
    char* newbuf;
    newbuf = (char*)nn_memory_realloc(ab->b, ab->len + len);
    if(newbuf == NULL)
    {
        return;
    }
    memcpy(newbuf + ab->len, s, len);
    ab->b = newbuf;
    ab->len += len;
}

void lino_appendbuf_destroy(linobuffer_t* ab)
{
    nn_memory_free(ab->b);
}

/* Helper of lino_util_refreshsingleline() and lino_util_refreshmultiline() to show hints
 * to the right of the prompt. */
void lino_refreshshowhints(linobuffer_t* ab, linostate_t* l, int pcollen)
{
    int bold;
    int color;
    int hintlen;
    int hintmaxlen;
    size_t collen;
    char* hint;
    char seq[64];
    collen = pcollen + lino_util_columnpos(l->buf, l->len, l->len);
    if(g_hintscallback && collen < l->cols)
    {
        color = -1;
        bold = 0;
        hint = g_hintscallback(l->buf, &color, &bold);
        if(hint)
        {
            hintlen = strlen(hint);
            hintmaxlen = l->cols - collen;
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
            if(g_freehintscallback)
            {
                g_freehintscallback(hint);
            }
        }
    }
}

/* Check if text is an ANSI escape sequence
 */
int lino_util_isansiescape(const char* buf, size_t buflen, size_t* len)
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
size_t lino_util_prompttextcolumnlen(const char* prompt, size_t plen)
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
    return lino_util_columnpos(buf, buflen, buflen);
}

/* Single line low level line refresh.
 *
 * Rewrite the currently edited line accordingly to the buffer content,
 * cursor position, and number of columns of the terminal. */
void lino_util_refreshsingleline(linostate_t* l)
{
    int fd;
    int chlen;
    size_t pcollen;
    size_t len;
    size_t pos;
    char* buf;
    char seq[64];
    linobuffer_t ab;
    pcollen = lino_util_prompttextcolumnlen(l->prompt, strlen(l->prompt));
    fd = l->ofd;
    buf = l->buf;
    len = l->len;
    pos = l->pos;
    while((pcollen + lino_util_columnpos(buf, len, pos)) >= l->cols)
    {
        chlen = g_fnnextcharlen(buf, len, 0, NULL);
        buf += chlen;
        len -= chlen;
        pos -= chlen;
    }
    while(pcollen + lino_util_columnpos(buf, len, len) > l->cols)
    {
        len -= g_fnprevcharlen(buf, len, len, NULL);
    }
    lino_appendbuf_init(&ab);
    /* Cursor to left edge */
    snprintf(seq, 64, "\r");
    lino_appendbuf_append(&ab, seq, strlen(seq));
    /* Write the prompt and the current buffer content */
    lino_appendbuf_append(&ab, l->prompt, strlen(l->prompt));
    if(g_maskmode == 1)
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
    lino_refreshshowhints(&ab, l, pcollen);
    /* Erase to right */
    snprintf(seq, 64, "\x1b[0K");
    lino_appendbuf_append(&ab, seq, strlen(seq));
    /* Move cursor to original position. */
    snprintf(seq, 64, "\r\x1b[%dC", (int)(lino_util_columnpos(buf, len, pos) + pcollen));
    lino_appendbuf_append(&ab, seq, strlen(seq));
    if(write(fd, ab.b, ab.len) == -1)
    {
    } /* Can't recover from write error. */
    lino_appendbuf_destroy(&ab);
}

/* Multi line low level line refresh.
 *
 * Rewrite the currently edited line accordingly to the buffer content,
 * cursor position, and number of columns of the terminal. */
void lino_util_refreshmultiline(linostate_t* l)
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
    pcollen = lino_util_prompttextcolumnlen(l->prompt, strlen(l->prompt));
    colpos = lino_util_columnposformultiline(l->buf, l->len, l->len, l->cols, pcollen);
    rows = (pcollen + colpos + l->cols - 1) / l->cols; /* rows used by current buf. */
    rpos = (pcollen + l->oldcolpos + l->cols) / l->cols; /* cursor relative row. */
    oldrows = l->maxrows;
    fd = l->ofd;
    /* Update maxrows if needed. */
    if(rows > (int)l->maxrows)
    {
        l->maxrows = rows;
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
    lino_appendbuf_append(&ab, l->prompt, strlen(l->prompt));
    if(g_maskmode == 1)
    {
        for(i = 0; i < l->len; i++)
        {
            lino_appendbuf_append(&ab, "*", 1);
        }
    }
    else
    {
        lino_appendbuf_append(&ab, l->buf, l->len);
    }
    /* Show hits if any. */
    lino_refreshshowhints(&ab, l, pcollen);
    /* Get column length to cursor position */
    colpos2 = lino_util_columnposformultiline(l->buf, l->len, l->pos, l->cols, pcollen);
    /* If we are at the very end of the screen with our prompt, we need to
   * emit a newline and move the prompt to the first column. */
    if(l->pos && l->pos == l->len && (colpos2 + pcollen) % l->cols == 0)
    {
        lndebug("<newline>");
        lino_appendbuf_append(&ab, "\n", 1);
        snprintf(seq, 64, "\r");
        lino_appendbuf_append(&ab, seq, strlen(seq));
        rows++;
        if(rows > (int)l->maxrows)
        {
            l->maxrows = rows;
        }
    }
    /* Move cursor to right position. */
    rpos2 = (pcollen + colpos2 + l->cols) / l->cols; /* current cursor relative row. */
    lndebug("rpos2 %d", rpos2);
    /* Go up till we reach the expected positon. */
    if(rows - rpos2 > 0)
    {
        lndebug("go-up %d", rows - rpos2);
        snprintf(seq, 64, "\x1b[%dA", rows - rpos2);
        lino_appendbuf_append(&ab, seq, strlen(seq));
    }
    /* Set column. */
    col = (pcollen + colpos2) % l->cols;
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
    l->oldcolpos = colpos2;
    if(write(fd, ab.b, ab.len) == -1)
    {
    } /* Can't recover from write error. */
    lino_appendbuf_destroy(&ab);
}

/* Calls the two low level functions lino_util_refreshsingleline() or
 * lino_util_refreshmultiline() according to the selected mode. */
void lino_refreshline(linostate_t* l)
{
    if(g_mlmode)
    {
        lino_util_refreshmultiline(l);
    }
    else
    {
        lino_util_refreshsingleline(l);
    }
}

/* Insert the character 'c' at cursor current position.
 *
 * On error writing to the terminal -1 is returned, otherwise 0. */
int lino_editinsert(linostate_t* l, const char* cbuf, int clen)
{
    if(l->len + clen <= l->buflen)
    {
        if(l->len == l->pos)
        {
            memcpy(&l->buf[l->pos], cbuf, clen);
            l->pos += clen;
            l->len += clen;
            l->buf[l->len] = '\0';
            if((!g_mlmode && lino_util_prompttextcolumnlen(l->prompt, l->plen) + lino_util_columnpos(l->buf, l->len, l->len) < l->cols && !g_hintscallback))
            {
                /* Avoid a full update of the line in the trivial case. */
                if(g_maskmode == 1)
                {
                    static const char d = '*';
                    if(write(l->ofd, &d, 1) == -1)
                    {
                        return -1;
                    }
                }
                else
                {
                    if(write(l->ofd, cbuf, clen) == -1)
                        return -1;
                }
            }
            else
            {
                lino_refreshline(l);
            }
        }
        else
        {
            memmove(l->buf + l->pos + clen, l->buf + l->pos, l->len - l->pos);
            memcpy(&l->buf[l->pos], cbuf, clen);
            l->pos += clen;
            l->len += clen;
            l->buf[l->len] = '\0';
            lino_refreshline(l);
        }
    }
    return 0;
}

/* Move cursor on the left. */
void lino_editmoveleft(linostate_t* l)
{
    if(l->pos > 0)
    {
        l->pos -= g_fnprevcharlen(l->buf, l->len, l->pos, NULL);
        lino_refreshline(l);
    }
}

/* Move cursor on the right. */
void lino_editmoveright(linostate_t* l)
{
    if(l->pos != l->len)
    {
        l->pos += g_fnnextcharlen(l->buf, l->len, l->pos, NULL);
        lino_refreshline(l);
    }
}

/* Move cursor to the start of the line. */
void lino_editmovehome(linostate_t* l)
{
    if(l->pos != 0)
    {
        l->pos = 0;
        lino_refreshline(l);
    }
}

/* Move cursor to the end of the line. */
void lino_edit_moveend(linostate_t* l)
{
    if(l->pos != l->len)
    {
        l->pos = l->len;
        lino_refreshline(l);
    }
}

/* Substitute the currently edited line with the next or previous history
 * entry as specified by 'dir'. */
#define LINENOISE_HISTORY_NEXT 0
#define LINENOISE_HISTORY_PREV 1

void lino_edithistorynext(linostate_t* l, int dir)
{
    if(g_historylength > 1)
    {
        /* Update the current history entry before to
     * overwrite it with the next one. */
        nn_memory_free(g_historybuflines[g_historylength - 1 - l->history_index]);
        g_historybuflines[g_historylength - 1 - l->history_index] = strdup(l->buf);
        /* Show the new entry */
        l->history_index += (dir == LINENOISE_HISTORY_PREV) ? 1 : -1;
        if(l->history_index < 0)
        {
            l->history_index = 0;
            return;
        }
        else if(l->history_index >= g_historylength)
        {
            l->history_index = g_historylength - 1;
            return;
        }
        strncpy(l->buf, g_historybuflines[g_historylength - 1 - l->history_index], l->buflen);
        l->buf[l->buflen - 1] = '\0';
        l->len = l->pos = strlen(l->buf);
        lino_refreshline(l);
    }
}

/* Delete the character at the right of the cursor without altering the cursor
 * position. Basically this is what happens with the "Delete" keyboard key. */
void lino_editdelete(linostate_t* l)
{
    int chlen;
    if(l->len > 0 && l->pos < l->len)
    {
        chlen = g_fnnextcharlen(l->buf, l->len, l->pos, NULL);
        memmove(l->buf + l->pos, l->buf + l->pos + chlen, l->len - l->pos - chlen);
        l->len -= chlen;
        l->buf[l->len] = '\0';
        lino_refreshline(l);
    }
}

/* Backspace implementation. */
void lino_editbackspace(linostate_t* l)
{
    int chlen;
    if(l->pos > 0 && l->len > 0)
    {
        chlen = g_fnprevcharlen(l->buf, l->len, l->pos, NULL);
        memmove(l->buf + l->pos - chlen, l->buf + l->pos, l->len - l->pos);
        l->pos -= chlen;
        l->len -= chlen;
        l->buf[l->len] = '\0';
        lino_refreshline(l);
    }
}

/* Delete the previosu word, maintaining the cursor at the start of the
 * current word. */
void lino_editdelprevword(linostate_t* l)
{
    size_t diff;
    size_t oldpos;
    oldpos = l->pos;
    while(l->pos > 0 && l->buf[l->pos - 1] == ' ')
    {
        l->pos--;
    }
    while(l->pos > 0 && l->buf[l->pos - 1] != ' ')
    {
        l->pos--;
    }
    diff = oldpos - l->pos;
    memmove(l->buf + l->pos, l->buf + oldpos, l->len - oldpos + 1);
    l->len -= diff;
    lino_refreshline(l);
}

/* This function is the core of the line editing capability of linenoise.
 * It expects 'fd' to be already in "raw mode" so that every key pressed
 * will be returned ASAP to read().
 *
 * The resulting string is put into 'buf' when the user type enter, or
 * when ctrl+d is typed.
 *
 * The function returns the length of the current buffer. */
int lino_editline(int stdin_fd, int stdout_fd, char* buf, size_t buflen, const char* prompt)
{
    int c;
    int nread;
    char seq[3];
    char cbuf[32];/* large enough for any encoding? */
    linostate_t l;
    /* Populate the linenoise state that we pass to functions implementing
   * specific editing functionalities. */
    l.ifd = stdin_fd;
    l.ofd = stdout_fd;
    l.buf = buf;
    l.buflen = buflen;
    l.prompt = prompt;
    l.plen = strlen(prompt);
    l.oldcolpos = l.pos = 0;
    l.len = 0;
    l.cols = lino_util_getcolumns(stdin_fd, stdout_fd);
    l.maxrows = 0;
    l.history_index = 0;
    /* Buffer starts empty. */
    l.buf[0] = '\0';
    l.buflen--; /* Make sure there is always space for the nulterm */
    /* The latest history entry is always our current buffer, that initially is just an empty string. */
    lino_historyadd("");
    if(write(l.ofd, prompt, l.plen) == -1)
    {
        return -1;
    }
    while(1)
    {
        nread = g_fnreadcode(l.ifd, cbuf, sizeof(cbuf), &c);
        if(nread <= 0)
        {
            return l.len;
        }
        /*
        * Only autocomplete when the callback is set. It returns < 0 when
        * there was an error reading from fd. Otherwise it will return the
        * character that should be handled next.
        */
        if(c == 9 && g_completioncallback != NULL)
        {
            nread = lino_completeline(&l, cbuf, sizeof(cbuf), &c);
            /* Return on errors */
            if(c < 0)
            {
                return l.len;
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
                    linofnhintsfunc_t* hc;
                    g_historylength--;
                    nn_memory_free(g_historybuflines[g_historylength]);
                    if(g_mlmode)
                    {
                        lino_edit_moveend(&l);
                    }
                    if(g_hintscallback)
                    {
                        /* Force a refresh without hints to leave the previous line as the user typed it after a newline. */
                        hc = g_hintscallback;
                        g_hintscallback = NULL;
                        lino_refreshline(&l);
                        g_hintscallback = hc;
                    }
                    return (int)l.len;
                }
                break;
            case LINO_KEY_CTRLC: /* ctrl-c, clear line */
                {
                    buf[0] = '\0';
                    l.pos = l.len = 0;
                    lino_refreshline(&l);
                }
                break;
            case LINO_KEY_BACKSPACE: /* backspace */
            case 8: /* ctrl-h */
                {
                    lino_editbackspace(&l);
                }
                break;
            case LINO_KEY_CTRLD: /* ctrl-d, act as end-of-file. */
                {
                    g_historylength--;
                    nn_memory_free(g_historybuflines[g_historylength]);
                    return -1;
                }
                break;
            case LINO_KEY_CTRLT: /* ctrl-t, swaps current character with previous. */
                {
                    int aux;
                    if(l.pos > 0 && l.pos < l.len)
                    {
                        aux = buf[l.pos - 1];
                        buf[l.pos - 1] = buf[l.pos];
                        buf[l.pos] = aux;
                        if(l.pos != l.len - 1)
                        {
                            l.pos++;
                        }
                        lino_refreshline(&l);
                    }
                }
                break;
            case LINO_KEY_CTRLB: /* ctrl-b */
                {
                    lino_editmoveleft(&l);
                }
                break;
            case LINO_KEY_CTRLF: /* ctrl-f */
                {
                    lino_editmoveright(&l);
                }
                break;
            case LINO_KEY_CTRLP: /* ctrl-p */
                {
                    lino_edithistorynext(&l, LINENOISE_HISTORY_PREV);
                }
                break;
            case LINO_KEY_CTRLN: /* ctrl-n */
                {
                    lino_edithistorynext(&l, LINENOISE_HISTORY_NEXT);
                }
                break;
            case LINO_KEY_ESC: /* escape sequence */
                {
                    /*
                    * Read the next two bytes representing the escape sequence.
                    * Use two calls to handle slow terminals returning the two
                    * chars at different times.
                    */
                    if(read(l.ifd, seq, 1) == -1)
                    {
                        break;
                    }
                    if(read(l.ifd, seq + 1, 1) == -1)
                    {
                        break;
                    }
                    /* ESC [ sequences. */
                    if(seq[0] == '[')
                    {
                        if(seq[1] >= '0' && seq[1] <= '9')
                        {
                            /* Extended escape, read additional byte. */
                            if(read(l.ifd, seq + 2, 1) == -1)
                            {
                                break;
                            }
                            if(seq[2] == '~')
                            {
                                switch(seq[1])
                                {
                                    case '3': /* Delete key. */
                                        {
                                            lino_editdelete(&l);
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
                                        lino_edithistorynext(&l, LINENOISE_HISTORY_PREV);
                                    }
                                    break;
                                case 'B': /* Down */
                                    {
                                        lino_edithistorynext(&l, LINENOISE_HISTORY_NEXT);
                                    }
                                    break;
                                case 'C': /* Right */
                                    {
                                        lino_editmoveright(&l);
                                    }
                                    break;
                                case 'D': /* Left */
                                    {
                                        lino_editmoveleft(&l);
                                    }
                                    break;
                                case 'H': /* Home */
                                    {
                                        lino_editmovehome(&l);
                                    }
                                    break;
                                case 'F': /* End*/
                                    {
                                        lino_edit_moveend(&l);
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
                                    lino_editmovehome(&l);
                                }
                                break;
                            case 'F': /* End*/
                                {
                                    lino_edit_moveend(&l);
                                }
                                break;
                        }
                    }
                }
                break;
            default:
                {
                    if(lino_editinsert(&l, cbuf, nread))
                    {
                        return -1;
                    }
                }
                break;
            case LINO_KEY_CTRLU: /* Ctrl+u, delete the whole line. */
                {
                    buf[0] = '\0';
                    l.pos = l.len = 0;
                    lino_refreshline(&l);
                }
                break;
            case LINO_KEY_CTRLK: /* Ctrl+k, delete from current to end of line. */
                buf[l.pos] = '\0';
                l.len = l.pos;
                lino_refreshline(&l);
                break;
            case LINO_KEY_CTRLA: /* Ctrl+a, go to the start of the line */
                lino_editmovehome(&l);
                break;
            case LINO_KEY_CTRLE: /* ctrl+e, go to the end of the line */
                lino_edit_moveend(&l);
                break;
            case LINO_KEY_CTRLL: /* ctrl+l, clear screen */
                lino_clearscreen();
                lino_refreshline(&l);
                break;
            case LINO_KEY_CTRLW: /* ctrl+w, delete previous word */
                lino_editdelprevword(&l);
                break;
        }
    }
    return l.len;
}

/* This special mode is used by linenoise in order to print scan codes
 * on screen for debugging / development purposes. It is implemented
 * by the linenoise_example program using the --keycodes option. */
void lino_printkeycodes(void)
{
    char quit[4];

    printf("Linenoise key codes debugging mode.\n"
           "Press keys to see scan codes. Type 'quit' at any time to exit.\n");
    if(lino_util_enablerawmode(fileno(stdin)) == -1)
        return;
    memset(quit, ' ', 4);
    while(1)
    {
        char c;
        int nread;

        nread = read(fileno(stdin), &c, 1);
        if(nread <= 0)
            continue;
        memmove(quit, quit + 1, sizeof(quit) - 1); /* shift string to left. */
        quit[sizeof(quit) - 1] = c; /* Insert current char on the right. */
        if(memcmp(quit, "quit", sizeof(quit)) == 0)
            break;

        printf("'%c' %02x (%d) (type quit to exit)\n", isprint((int)c) ? c : '?', (int)c, (int)c);
        printf("\r"); /* Go left edge manually, we are in raw mode. */
        fflush(stdout);
    }
    lino_util_disablerawmode(fileno(stdin));
}

/* This function calls the line editing function lino_editline() using
 * the STDIN file descriptor set in raw mode. */
int lino_util_getraw(char* buf, size_t buflen, const char* prompt)
{
    int count;
    if(buflen == 0)
    {
        errno = EINVAL;
        return -1;
    }
    #if defined(LINENOISE_ISUNIX)
    if(lino_util_enablerawmode(fileno(stdin)) == -1)
    {
        return -1;
    }
    #endif
    count = lino_editline(fileno(stdin), fileno(stdout), buf, buflen, prompt);
    lino_util_disablerawmode(fileno(stdin));
    printf("\n");
    return count;
}

/* This function is called when linenoise() is called with the standard
 * input file descriptor not attached to a TTY. So for example when the
 * program using linenoise is called in pipe or with a file redirected
 * to its standard input. In this case, we want to be able to return the
 * line regardless of its length (by default we are limited to 4k). */
char* lino_notty(void)
{
    char* line = NULL;
    size_t len = 0, maxlen = 0;

    while(1)
    {
        if(len == maxlen)
        {
            if(maxlen == 0)
                maxlen = 16;
            maxlen *= 2;
            char* oldval = line;
            line = (char*)nn_memory_realloc(line, maxlen);
            if(line == NULL)
            {
                if(oldval)
                    nn_memory_free(oldval);
                return NULL;
            }
        }
        int c = fgetc(stdin);
        if(c == EOF || c == '\n')
        {
            if(c == EOF && len == 0)
            {
                nn_memory_free(line);
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
            line[len] = c;
            len++;
        }
    }
}

/* The high level function that is the main API of the linenoise library.
 * This function checks if the terminal has basic capabilities, just checking
 * for a blacklist of stupid terminals, and later either calls the line
 * editing function or uses dummy fgets() so that you will be able to type
 * something even in the most desperate of the conditions. */
char* lino_readline(const char* prompt)
{
    char buf[LINENOISE_MAX_LINE];
    int count;
    if(!isatty(fileno(stdin)))
    {
        #if defined(LINENOISE_ISUNIX)
        /*
        * Not a tty: read from file / pipe. In this mode we don't want any
        * limit to the line size, so we call a function to handle that.
        */
        return lino_notty();
        #endif
    }
    else if(lino_util_isunsupportedterm())
    {
        size_t len;
        printf("%s", prompt);
        fflush(stdout);
        if(fgets(buf, LINENOISE_MAX_LINE, stdin) == NULL)
            return NULL;
        len = strlen(buf);
        while(len && (buf[len - 1] == '\n' || buf[len - 1] == '\r'))
        {
            len--;
            buf[len] = '\0';
        }
        return strdup(buf);
    }
    count = lino_util_getraw(buf, LINENOISE_MAX_LINE, prompt);
    if(count == -1)
        return NULL;
    return strdup(buf);
}

/* This is just a wrapper the user may want to call in order to make sure
 * the linenoise returned buffer is freed with the same allocator it was
 * created with. Useful when the main program is using an alternative
 * allocator. */
void lino_freeline(void* ptr)
{
    nn_memory_free(ptr);
}

/* ================================ History ================================= */

/* Free the history, but does not reset it. Only used when we have to
 * exit() to avoid memory leaks are reported by valgrind & co. */
void lino_freehistory(void)
{
    if(g_historybuflines)
    {
        int j;

        for(j = 0; j < g_historylength; j++)
            nn_memory_free(g_historybuflines[j]);
        nn_memory_free(g_historybuflines);
    }
}

/* At exit we'll try to fix the terminal to the initial conditions. */
void lino_atexit(void)
{
    lino_util_disablerawmode(fileno(stdin));
    lino_freehistory();
}

/* This is the API call to add a new entry in the linenoise history.
 * It uses a fixed array of char pointers that are shifted (memmoved)
 * when the history max length is reached in order to remove the older
 * entry and make room for the new one, so it is not exactly suitable for huge
 * histories, but will work well for a few hundred of entries.
 *
 * Using a circular buffer is smarter, but a bit more complex to handle. */
int lino_historyadd(const char* line)
{
    char* linecopy;

    if(g_historymaxlen == 0)
        return 0;

    /* Initialization on first call. */
    if(g_historybuflines == NULL)
    {
        g_historybuflines = (char**)nn_memory_malloc(sizeof(char*) * g_historymaxlen);
        if(g_historybuflines == NULL)
            return 0;
        memset(g_historybuflines, 0, (sizeof(char*) * g_historymaxlen));
    }

    /* Don't add duplicated lines. */
    if(g_historylength && !strcmp(g_historybuflines[g_historylength - 1], line))
        return 0;

    /* Add an heap allocated copy of the line in the history.
   * If we reached the max length, remove the older line. */
    linecopy = strdup(line);
    if(!linecopy)
        return 0;
    if(g_historylength == g_historymaxlen)
    {
        nn_memory_free(g_historybuflines[0]);
        memmove(g_historybuflines, g_historybuflines + 1, sizeof(char*) * (g_historymaxlen - 1));
        g_historylength--;
    }
    g_historybuflines[g_historylength] = linecopy;
    g_historylength++;
    return 1;
}

/* Set the maximum length for the history. This function can be called even
 * if there is already some history, the function will make sure to retain
 * just the latest 'len' elements if the new history length value is smaller
 * than the amount of items already inside the history. */
int lino_historysetmaxlength(int len)
{
    char** newbuf;

    if(len < 1)
        return 0;
    if(g_historybuflines)
    {
        int tocopy = g_historylength;

        newbuf = (char**)nn_memory_malloc(sizeof(char*) * len);
        if(newbuf == NULL)
            return 0;

        /* If we can't copy everything, free the elements we'll not use. */
        if(len < tocopy)
        {
            int j;

            for(j = 0; j < tocopy - len; j++)
                nn_memory_free(g_historybuflines[j]);
            tocopy = len;
        }
        memset(newbuf, 0, sizeof(char*) * len);
        memcpy(newbuf, g_historybuflines + (g_historylength - tocopy), sizeof(char*) * tocopy);
        nn_memory_free(g_historybuflines);
        g_historybuflines = newbuf;
    }
    g_historymaxlen = len;
    if(g_historylength > g_historymaxlen)
        g_historylength = g_historymaxlen;
    return 1;
}

/* Save the history in the specified file. On success 0 is returned
 * otherwise -1 is returned. */
int lino_historysavetofile(const char* filename)
{
    #if defined(LINENOISE_ISUNIX)
    mode_t old_umask = umask(S_IXUSR | S_IRWXG | S_IRWXO);
    FILE* fp;
    int j;

    fp = fopen(filename, "w");
    umask(old_umask);
    if(fp == NULL)
        return -1;
    chmod(filename, S_IRUSR | S_IWUSR);
    for(j = 0; j < g_historylength; j++)
        fprintf(fp, "%s\n", g_historybuflines[j]);
    fclose(fp);
    #endif
    return 0;
}

/* Load the history from the specified file. If the file does not exist
 * zero is returned and no operation is performed.
 *
 * If the file exists and the operation succeeded 0 is returned, otherwise
 * on error -1 is returned. */
int lino_historyloadfromfile(const char* filename)
{
    FILE* fp = fopen(filename, "r");
    char buf[LINENOISE_MAX_LINE];

    if(fp == NULL)
        return -1;

    while(fgets(buf, LINENOISE_MAX_LINE, fp) != NULL)
    {
        char* p;

        p = strchr(buf, '\r');
        if(!p)
            p = strchr(buf, '\n');
        if(p)
            *p = '\0';
        lino_historyadd(buf);
    }
    fclose(fp);
    return 0;
}
