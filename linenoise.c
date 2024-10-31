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
 * When linenoiseClearScreen() is called, two additional escape sequences
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

#include "linenoise.h"

#define LINENOISE_DEFAULT_HISTORY_MAX_LEN 100
#define LINENOISE_MAX_LINE 4096
#define UNUSED(x) (void)(x)

#if defined(__linux__) || defined(__unix__)
    #define LINENOISE_ISUNIX
#endif


static const char* unsupported_term[] = { "dumb", "cons25", "emacs", NULL };
static linenoiseCompletionCallback* completionCallback = NULL;
static linenoiseHintsCallback* hintsCallback = NULL;
static linenoiseFreeHintsCallback* freeHintsCallback = NULL;

#if defined(LINENOISE_ISUNIX)
static struct termios orig_termios; /* In order to restore at exit.*/
#endif
static int maskmode = 0; /* Show "***" instead of input. For passwords. */
static int rawmode = 0; /* For atexit() function to check if restore is needed*/
static int mlmode = 0; /* Multi line mode. Default is single line. */
static int atexit_registered = 0; /* Register atexit just 1 time. */
static int history_max_len = LINENOISE_DEFAULT_HISTORY_MAX_LEN;
static int history_len = 0;
static char** history = NULL;



void linenoiseAtExit(void);
int linenoiseHistoryAdd(const char* line);
void lino_refreshline(linenoiseState* l);

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
                        rpos, (int)l->maxrows, old_rows);                                                                                                   \
            }                                                                                                                                               \
            fprintf(lndebug_fp, ", " __VA_ARGS__);                                                                                                          \
            fflush(lndebug_fp);                                                                                                                             \
        } while(0)
#else
    #define lndebug(fmt, ...)
#endif


/* ========================== Encoding functions ============================= */

static const char linenoise_strcmp_charmap[] = {
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

int linenoise_strcasecmp(const char* s1, const char* s2)
{
    const char *cm = linenoise_strcmp_charmap;
	while (cm[(int)*s1] == cm[(int)*s2++])
    {
		if (*s1++ == '\0')
        {
            return(0);
        }
	}
    return(cm[(int)*s1] - cm[(int)*--s2]);
}

int linenoise_strncasecmp(const char* s1, const char* s2, int n)
{
	const char *cm = linenoise_strcmp_charmap;
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
size_t lino_util_defaultprevcharlen(const char* buf, size_t buf_len, size_t pos, size_t* col_len)
{
    UNUSED(buf);
    UNUSED(buf_len);
    UNUSED(pos);
    if(col_len != NULL)
        *col_len = 1;
    return 1;
}

/* Get byte length and column length of the next character */
size_t lino_util_defaultnextcharlen(const char* buf, size_t buf_len, size_t pos, size_t* col_len)
{
    UNUSED(buf);
    UNUSED(buf_len);
    UNUSED(pos);
    if(col_len != NULL)
        *col_len = 1;
    return 1;
}

/* Read bytes of the next character */
size_t lino_util_defaultreadcode(int fd, char* buf, size_t buf_len, int* c)
{
    if(buf_len < 1)
        return -1;
    int nread = read(fd, &buf[0], 1);
    if(nread == 1)
        *c = buf[0];
    return nread;
}

/* Set default encoding functions */
static linenoisePrevCharLen* prevCharLen = lino_util_defaultprevcharlen;
static linenoiseNextCharLen* nextCharLen = lino_util_defaultnextcharlen;
static linenoiseReadCode* readCode = lino_util_defaultreadcode;

/* Set used defined encoding functions */
void linenoiseSetEncodingFunctions(linenoisePrevCharLen* prevCharLenFunc, linenoiseNextCharLen* nextCharLenFunc, linenoiseReadCode* readCodeFunc)
{
    prevCharLen = prevCharLenFunc;
    nextCharLen = nextCharLenFunc;
    readCode = readCodeFunc;
}

/* Get column length from begining of buffer to current byte position */
size_t lino_util_columnpos(const char* buf, size_t buf_len, size_t pos)
{
    size_t ret = 0;
    size_t off = 0;
    while(off < pos)
    {
        size_t col_len;
        size_t len = nextCharLen(buf, buf_len, off, &col_len);
        off += len;
        ret += col_len;
    }
    return ret;
}

/* Get column length from begining of buffer to current byte position for multiline mode*/
size_t lino_util_columnposformultiline(const char* buf, size_t buf_len, size_t pos, size_t cols, size_t ini_pos)
{
    size_t ret = 0;
    size_t colwid = ini_pos;

    size_t off = 0;
    while(off < buf_len)
    {
        size_t col_len;
        size_t len = nextCharLen(buf, buf_len, off, &col_len);

        int dif = (int)(colwid + col_len) - (int)cols;
        if(dif > 0)
        {
            ret += dif;
            colwid = col_len;
        }
        else if(dif == 0)
        {
            colwid = 0;
        }
        else
        {
            colwid += col_len;
        }

        if(off >= pos)
            break;
        off += len;
        ret += col_len;
    }

    return ret;
}

/* ======================= Low level terminal handling ====================== */

/* Enable "mask mode". When it is enabled, instead of the input that
 * the user is typing, the terminal will just display a corresponding
 * number of asterisks, like "****". This is useful for passwords and other
 * secrets that should not be displayed. */
void linenoiseMaskModeEnable(void)
{
    maskmode = 1;
}

/* Disable mask mode. */
void linenoiseMaskModeDisable(void)
{
    maskmode = 0;
}

/* Set if to use or not the multi line mode. */
void linenoiseSetMultiLine(int ml)
{
    mlmode = ml;
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
    for(j = 0; unsupported_term[j]; j++)
    {
        if(!linenoise_strcasecmp(term, unsupported_term[j]))
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
        goto fatal;
    if(!atexit_registered)
    {
        atexit(linenoiseAtExit);
        atexit_registered = 1;
    }
    if(tcgetattr(fd, &orig_termios) == -1)
        goto fatal;

    raw = orig_termios; /* modify the original mode */
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
        goto fatal;
    rawmode = 1;
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
    if(rawmode && tcsetattr(fd, TCSADRAIN, &orig_termios) != -1)
        rawmode = 0;
    #endif
}

/* Use the ESC [6n escape sequence to query the horizontal cursor position
 * and return it. On error -1 is returned, on success the position of the
 * cursor. */
int lino_util_getcursorposition(int ifd, int ofd)
{
    char buf[32];
    int cols, rows;
    unsigned int i = 0;

    /* Report cursor location */
    if(write(ofd, "\x1b[6n", 4) != 4)
        return -1;

    /* Read the response: ESC [ rows ; cols R */
    while(i < sizeof(buf) - 1)
    {
        if(read(ifd, buf + i, 1) != 1)
            break;
        if(buf[i] == 'R')
            break;
        i++;
    }
    buf[i] = '\0';

    /* Parse it. */
    if(buf[0] != LINENOISE_KEY_ESC || buf[1] != '[')
        return -1;
    if(sscanf(buf + 2, "%d;%d", &rows, &cols) != 2)
        return -1;
    return cols;
}

/* Try to get the number of columns in the current terminal, or assume 80
 * if it fails. */
int lino_util_getcolumns(int ifd, int ofd)
{
    #if defined(LINENOISE_ISUNIX)
    struct winsize ws;

    if(ioctl(1, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0)
    {
        /* ioctl() failed. Try to query the terminal itself. */
        int start, cols;

        /* Get the initial position so we can restore it later. */
        start = lino_util_getcursorposition(ifd, ofd);
        if(start == -1)
            goto failed;

        /* Go to right margin and get position. */
        if(write(ofd, "\x1b[999C", 6) != 6)
            goto failed;
        cols = lino_util_getcursorposition(ifd, ofd);
        if(cols == -1)
            goto failed;

        /* Restore position. */
        if(cols > start)
        {
            char seq[32];
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
void linenoiseClearScreen(void)
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

/* Free a list of completion option populated by linenoiseAddCompletion(). */
void lino_freecompletions(linenoiseCompletions* lc)
{
    size_t i;
    for(i = 0; i < lc->len; i++)
        free(lc->cvec[i]);
    if(lc->cvec != NULL)
        free(lc->cvec);
}

/* This is an helper function for linenoiseEdit() and is called when the
 * user types the <tab> key in order to complete the string currently in the
 * input.
 *
 * The state of the editing is encapsulated into the pointed linenoiseState
 * structure as described in the structure definition. */
int lino_completeline(linenoiseState* ls, char* cbuf, size_t cbuf_len, int* c)
{
    linenoiseCompletions lc = { 0, NULL };
    int nread = 0, nwritten;
    *c = 0;

    completionCallback(ls->buf, &lc);
    if(lc.len == 0)
    {
        lino_util_beep();
    }
    else
    {
        size_t stop = 0, i = 0;

        while(!stop)
        {
            /* Show completion or original buffer */
            if(i < lc.len)
            {
                linenoiseState saved = *ls;

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

            nread = readCode(ls->ifd, cbuf, cbuf_len, c);
            if(nread <= 0)
            {
                lino_freecompletions(&lc);
                *c = -1;
                return nread;
            }

            switch(*c)
            {
                case 9: /* tab */
                    i = (i + 1) % (lc.len + 1);
                    if(i == lc.len)
                        lino_util_beep();
                    break;
                case 27: /* escape */
                    /* Re-show original buffer */
                    if(i < lc.len)
                        lino_refreshline(ls);
                    stop = 1;
                    break;
                default:
                    /* Update buffer and return */
                    if(i < lc.len)
                    {
                        nwritten = snprintf(ls->buf, ls->buflen, "%s", lc.cvec[i]);
                        ls->len = ls->pos = nwritten;
                    }
                    stop = 1;
                    break;
            }
        }
    }

    lino_freecompletions(&lc);
    return nread;
}

/* Register a callback function to be called for tab-completion. */
void linenoiseSetCompletionCallback(linenoiseCompletionCallback* fn)
{
    completionCallback = fn;
}

/* Register a hits function to be called to show hits to the user at the
 * right of the prompt. */
void linenoiseSetHintsCallback(linenoiseHintsCallback* fn)
{
    hintsCallback = fn;
}

/* Register a function to free the hints returned by the hints callback
 * registered with linenoiseSetHintsCallback(). */
void linenoiseSetFreeHintsCallback(linenoiseFreeHintsCallback* fn)
{
    freeHintsCallback = fn;
}

/* This function is used by the callback function registered by the user
 * in order to add completion options given the input string when the
 * user typed <tab>. See the example.c source code for a very easy to
 * understand example. */
void linenoiseAddCompletion(linenoiseCompletions* lc, const char* str)
{
    size_t len;
    char *copy;
    char** cvec;
    len = strlen(str);
    copy = (char*)malloc(len + 1);
    if(copy == NULL)
    {
        return;
    }
    memcpy(copy, str, len + 1);
    cvec = (char**)realloc(lc->cvec, sizeof(char*) * (lc->len + 1));
    if(cvec == NULL)
    {
        free(copy);
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

void lino_appendbuf_init(LNAppendBuffer* ab)
{
    ab->b = NULL;
    ab->len = 0;
}

void lino_appendbuf_append(LNAppendBuffer* ab, const char* s, int len)
{
    char* newbuf;
    newbuf = (char*)realloc(ab->b, ab->len + len);
    if(newbuf == NULL)
    {
        return;
    }
    memcpy(newbuf + ab->len, s, len);
    ab->b = newbuf;
    ab->len += len;
}

void lino_appendbuf_destroy(LNAppendBuffer* ab)
{
    free(ab->b);
}

/* Helper of lino_util_refreshsingleline() and lino_util_refreshmultiline() to show hints
 * to the right of the prompt. */
void lino_refreshshowhints(LNAppendBuffer* ab, linenoiseState* l, int pcollen)
{
    char seq[64];
    size_t collen = pcollen + lino_util_columnpos(l->buf, l->len, l->len);
    if(hintsCallback && collen < l->cols)
    {
        int color = -1, bold = 0;
        char* hint = hintsCallback(l->buf, &color, &bold);
        if(hint)
        {
            int hintlen = strlen(hint);
            int hintmaxlen = l->cols - collen;
            if(hintlen > hintmaxlen)
                hintlen = hintmaxlen;
            if(bold == 1 && color == -1)
                color = 37;
            if(color != -1 || bold != 0)
                snprintf(seq, 64, "\033[%d;%d;49m", bold, color);
            else
                seq[0] = '\0';
            lino_appendbuf_append(ab, seq, strlen(seq));
            lino_appendbuf_append(ab, hint, hintlen);
            if(color != -1 || bold != 0)
                lino_appendbuf_append(ab, "\033[0m", 4);
            /* Call the function to free the hint returned. */
            if(freeHintsCallback)
                freeHintsCallback(hint);
        }
    }
}

/* Check if text is an ANSI escape sequence
 */
int lino_util_isansiescape(const char* buf, size_t buf_len, size_t* len)
{
    if(buf_len > 2 && !memcmp("\033[", buf, 2))
    {
        size_t off = 2;
        while(off < buf_len)
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
                    *len = off;
                    return 1;
            }
        }
    }
    return 0;
}

/* Get column length of prompt text
 */
size_t lino_util_prompttextcolumnlen(const char* prompt, size_t plen)
{
    char buf[LINENOISE_MAX_LINE];
    size_t buf_len = 0;
    size_t off = 0;
    while(off < plen)
    {
        size_t len;
        if(lino_util_isansiescape(prompt + off, plen - off, &len))
        {
            off += len;
            continue;
        }
        buf[buf_len++] = prompt[off++];
    }
    return lino_util_columnpos(buf, buf_len, buf_len);
}

/* Single line low level line refresh.
 *
 * Rewrite the currently edited line accordingly to the buffer content,
 * cursor position, and number of columns of the terminal. */
void lino_util_refreshsingleline(linenoiseState* l)
{
    char seq[64];
    size_t pcollen = lino_util_prompttextcolumnlen(l->prompt, strlen(l->prompt));
    int fd = l->ofd;
    char* buf = l->buf;
    size_t len = l->len;
    size_t pos = l->pos;
    LNAppendBuffer ab;

    while((pcollen + lino_util_columnpos(buf, len, pos)) >= l->cols)
    {
        int chlen = nextCharLen(buf, len, 0, NULL);
        buf += chlen;
        len -= chlen;
        pos -= chlen;
    }
    while(pcollen + lino_util_columnpos(buf, len, len) > l->cols)
    {
        len -= prevCharLen(buf, len, len, NULL);
    }

    lino_appendbuf_init(&ab);
    /* Cursor to left edge */
    snprintf(seq, 64, "\r");
    lino_appendbuf_append(&ab, seq, strlen(seq));
    /* Write the prompt and the current buffer content */
    lino_appendbuf_append(&ab, l->prompt, strlen(l->prompt));
    if(maskmode == 1)
    {
        while(len--)
            lino_appendbuf_append(&ab, "*", 1);
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
void lino_util_refreshmultiline(linenoiseState* l)
{
    char seq[64];
    size_t pcollen = lino_util_prompttextcolumnlen(l->prompt, strlen(l->prompt));
    int colpos = lino_util_columnposformultiline(l->buf, l->len, l->len, l->cols, pcollen);
    int colpos2; /* cursor column position. */
    int rows = (pcollen + colpos + l->cols - 1) / l->cols; /* rows used by current buf. */
    int rpos = (pcollen + l->oldcolpos + l->cols) / l->cols; /* cursor relative row. */
    int rpos2; /* rpos after refresh. */
    int col; /* colum position, zero-based. */
    int old_rows = l->maxrows;
    int fd = l->ofd, j;
    LNAppendBuffer ab;

    /* Update maxrows if needed. */
    if(rows > (int)l->maxrows)
        l->maxrows = rows;

    /* First step: clear all the lines used before. To do so start by
   * going to the last row. */
    lino_appendbuf_init(&ab);
    if(old_rows - rpos > 0)
    {
        lndebug("go down %d", old_rows - rpos);
        snprintf(seq, 64, "\x1b[%dB", old_rows - rpos);
        lino_appendbuf_append(&ab, seq, strlen(seq));
    }

    /* Now for every row clear it, go up. */
    for(j = 0; j < old_rows - 1; j++)
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
    if(maskmode == 1)
    {
        unsigned int i;
        for(i = 0; i < l->len; i++)
            lino_appendbuf_append(&ab, "*", 1);
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
            l->maxrows = rows;
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
        snprintf(seq, 64, "\r\x1b[%dC", col);
    else
        snprintf(seq, 64, "\r");
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
void lino_refreshline(linenoiseState* l)
{
    if(mlmode)
        lino_util_refreshmultiline(l);
    else
        lino_util_refreshsingleline(l);
}

/* Insert the character 'c' at cursor current position.
 *
 * On error writing to the terminal -1 is returned, otherwise 0. */
int linenoiseEditInsert(linenoiseState* l, const char* cbuf, int clen)
{
    if(l->len + clen <= l->buflen)
    {
        if(l->len == l->pos)
        {
            memcpy(&l->buf[l->pos], cbuf, clen);
            l->pos += clen;
            l->len += clen;
            ;
            l->buf[l->len] = '\0';
            if((!mlmode && lino_util_prompttextcolumnlen(l->prompt, l->plen) + lino_util_columnpos(l->buf, l->len, l->len) < l->cols && !hintsCallback))
            {
                /* Avoid a full update of the line in the
         * trivial case. */
                if(maskmode == 1)
                {
                    static const char d = '*';
                    if(write(l->ofd, &d, 1) == -1)
                        return -1;
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
void linenoiseEditMoveLeft(linenoiseState* l)
{
    if(l->pos > 0)
    {
        l->pos -= prevCharLen(l->buf, l->len, l->pos, NULL);
        lino_refreshline(l);
    }
}

/* Move cursor on the right. */
void linenoiseEditMoveRight(linenoiseState* l)
{
    if(l->pos != l->len)
    {
        l->pos += nextCharLen(l->buf, l->len, l->pos, NULL);
        lino_refreshline(l);
    }
}

/* Move cursor to the start of the line. */
void linenoiseEditMoveHome(linenoiseState* l)
{
    if(l->pos != 0)
    {
        l->pos = 0;
        lino_refreshline(l);
    }
}

/* Move cursor to the end of the line. */
void linenoiseEditMoveEnd(linenoiseState* l)
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

void linenoiseEditHistoryNext(linenoiseState* l, int dir)
{
    if(history_len > 1)
    {
        /* Update the current history entry before to
     * overwrite it with the next one. */
        free(history[history_len - 1 - l->history_index]);
        history[history_len - 1 - l->history_index] = strdup(l->buf);
        /* Show the new entry */
        l->history_index += (dir == LINENOISE_HISTORY_PREV) ? 1 : -1;
        if(l->history_index < 0)
        {
            l->history_index = 0;
            return;
        }
        else if(l->history_index >= history_len)
        {
            l->history_index = history_len - 1;
            return;
        }
        strncpy(l->buf, history[history_len - 1 - l->history_index], l->buflen);
        l->buf[l->buflen - 1] = '\0';
        l->len = l->pos = strlen(l->buf);
        lino_refreshline(l);
    }
}

/* Delete the character at the right of the cursor without altering the cursor
 * position. Basically this is what happens with the "Delete" keyboard key. */
void linenoiseEditDelete(linenoiseState* l)
{
    if(l->len > 0 && l->pos < l->len)
    {
        int chlen = nextCharLen(l->buf, l->len, l->pos, NULL);
        memmove(l->buf + l->pos, l->buf + l->pos + chlen, l->len - l->pos - chlen);
        l->len -= chlen;
        l->buf[l->len] = '\0';
        lino_refreshline(l);
    }
}

/* Backspace implementation. */
void linenoiseEditBackspace(linenoiseState* l)
{
    if(l->pos > 0 && l->len > 0)
    {
        int chlen = prevCharLen(l->buf, l->len, l->pos, NULL);
        memmove(l->buf + l->pos - chlen, l->buf + l->pos, l->len - l->pos);
        l->pos -= chlen;
        l->len -= chlen;
        l->buf[l->len] = '\0';
        lino_refreshline(l);
    }
}

/* Delete the previosu word, maintaining the cursor at the start of the
 * current word. */
void linenoiseEditDeletePrevWord(linenoiseState* l)
{
    size_t old_pos = l->pos;
    size_t diff;

    while(l->pos > 0 && l->buf[l->pos - 1] == ' ')
        l->pos--;
    while(l->pos > 0 && l->buf[l->pos - 1] != ' ')
        l->pos--;
    diff = old_pos - l->pos;
    memmove(l->buf + l->pos, l->buf + old_pos, l->len - old_pos + 1);
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
int linenoiseEdit(int stdin_fd, int stdout_fd, char* buf, size_t buflen, const char* prompt)
{
    linenoiseState l;

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

    /* The latest history entry is always our current buffer, that
   * initially is just an empty string. */
    linenoiseHistoryAdd("");

    if(write(l.ofd, prompt, l.plen) == -1)
        return -1;
    while(1)
    {
        int c;
        char cbuf[32];/* large enough for any encoding? */
        int nread;
        char seq[3];

        nread = readCode(l.ifd, cbuf, sizeof(cbuf), &c);
        if(nread <= 0)
            return l.len;

        /* Only autocomplete when the callback is set. It returns < 0 when
     * there was an error reading from fd. Otherwise it will return the
     * character that should be handled next. */
        if(c == 9 && completionCallback != NULL)
        {
            nread = lino_completeline(&l, cbuf, sizeof(cbuf), &c);
            /* Return on errors */
            if(c < 0)
                return l.len;
            /* Read next character when 0 */
            if(c == 0)
                continue;
        }

        switch(c)
        {
            case LINENOISE_KEY_LINEFEED:
            case LINENOISE_KEY_ENTER: /* enter */
                history_len--;
                free(history[history_len]);
                if(mlmode)
                    linenoiseEditMoveEnd(&l);
                if(hintsCallback)
                {
                    /* Force a refresh without hints to leave the previous
         * line as the user typed it after a newline. */
                    linenoiseHintsCallback* hc = hintsCallback;
                    hintsCallback = NULL;
                    lino_refreshline(&l);
                    hintsCallback = hc;
                }
                return (int)l.len;
            case LINENOISE_KEY_CTRLC: /* ctrl-c, clear line */
                buf[0] = '\0';
                l.pos = l.len = 0;
                lino_refreshline(&l);
                break;
            case LINENOISE_KEY_BACKSPACE: /* backspace */
            case 8: /* ctrl-h */
                linenoiseEditBackspace(&l);
                break;
            case LINENOISE_KEY_CTRLD: /* ctrl-d, act as end-of-file. */
                history_len--;
                free(history[history_len]);
                return -1;
            case LINENOISE_KEY_CTRLT: /* ctrl-t, swaps current character with previous. */
                if(l.pos > 0 && l.pos < l.len)
                {
                    int aux = buf[l.pos - 1];
                    buf[l.pos - 1] = buf[l.pos];
                    buf[l.pos] = aux;
                    if(l.pos != l.len - 1)
                        l.pos++;
                    lino_refreshline(&l);
                }
                break;
            case LINENOISE_KEY_CTRLB: /* ctrl-b */
                linenoiseEditMoveLeft(&l);
                break;
            case LINENOISE_KEY_CTRLF: /* ctrl-f */
                linenoiseEditMoveRight(&l);
                break;
            case LINENOISE_KEY_CTRLP: /* ctrl-p */
                linenoiseEditHistoryNext(&l, LINENOISE_HISTORY_PREV);
                break;
            case LINENOISE_KEY_CTRLN: /* ctrl-n */
                linenoiseEditHistoryNext(&l, LINENOISE_HISTORY_NEXT);
                break;
            case LINENOISE_KEY_ESC: /* escape sequence */
                /* Read the next two bytes representing the escape sequence.
       * Use two calls to handle slow terminals returning the two
       * chars at different times. */
                if(read(l.ifd, seq, 1) == -1)
                    break;
                if(read(l.ifd, seq + 1, 1) == -1)
                    break;

                /* ESC [ sequences. */
                if(seq[0] == '[')
                {
                    if(seq[1] >= '0' && seq[1] <= '9')
                    {
                        /* Extended escape, read additional byte. */
                        if(read(l.ifd, seq + 2, 1) == -1)
                            break;
                        if(seq[2] == '~')
                        {
                            switch(seq[1])
                            {
                                case '3': /* Delete key. */
                                    linenoiseEditDelete(&l);
                                    break;
                            }
                        }
                    }
                    else
                    {
                        switch(seq[1])
                        {
                            case 'A': /* Up */
                                linenoiseEditHistoryNext(&l, LINENOISE_HISTORY_PREV);
                                break;
                            case 'B': /* Down */
                                linenoiseEditHistoryNext(&l, LINENOISE_HISTORY_NEXT);
                                break;
                            case 'C': /* Right */
                                linenoiseEditMoveRight(&l);
                                break;
                            case 'D': /* Left */
                                linenoiseEditMoveLeft(&l);
                                break;
                            case 'H': /* Home */
                                linenoiseEditMoveHome(&l);
                                break;
                            case 'F': /* End*/
                                linenoiseEditMoveEnd(&l);
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
                            linenoiseEditMoveHome(&l);
                            break;
                        case 'F': /* End*/
                            linenoiseEditMoveEnd(&l);
                            break;
                    }
                }
                break;
            default:
                if(linenoiseEditInsert(&l, cbuf, nread))
                    return -1;
                break;
            case LINENOISE_KEY_CTRLU: /* Ctrl+u, delete the whole line. */
                buf[0] = '\0';
                l.pos = l.len = 0;
                lino_refreshline(&l);
                break;
            case LINENOISE_KEY_CTRLK: /* Ctrl+k, delete from current to end of line. */
                buf[l.pos] = '\0';
                l.len = l.pos;
                lino_refreshline(&l);
                break;
            case LINENOISE_KEY_CTRLA: /* Ctrl+a, go to the start of the line */
                linenoiseEditMoveHome(&l);
                break;
            case LINENOISE_KEY_CTRLE: /* ctrl+e, go to the end of the line */
                linenoiseEditMoveEnd(&l);
                break;
            case LINENOISE_KEY_CTRLL: /* ctrl+l, clear screen */
                linenoiseClearScreen();
                lino_refreshline(&l);
                break;
            case LINENOISE_KEY_CTRLW: /* ctrl+w, delete previous word */
                linenoiseEditDeletePrevWord(&l);
                break;
        }
    }
    return l.len;
}

/* This special mode is used by linenoise in order to print scan codes
 * on screen for debugging / development purposes. It is implemented
 * by the linenoise_example program using the --keycodes option. */
void linenoisePrintKeyCodes(void)
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

/* This function calls the line editing function linenoiseEdit() using
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
    count = linenoiseEdit(fileno(stdin), fileno(stdout), buf, buflen, prompt);
    lino_util_disablerawmode(fileno(stdin));
    printf("\n");
    return count;
}

/* This function is called when linenoise() is called with the standard
 * input file descriptor not attached to a TTY. So for example when the
 * program using linenoise is called in pipe or with a file redirected
 * to its standard input. In this case, we want to be able to return the
 * line regardless of its length (by default we are limited to 4k). */
char* linenoiseNoTTY(void)
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
            line = (char*)realloc(line, maxlen);
            if(line == NULL)
            {
                if(oldval)
                    free(oldval);
                return NULL;
            }
        }
        int c = fgetc(stdin);
        if(c == EOF || c == '\n')
        {
            if(c == EOF && len == 0)
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
char* linenoise(const char* prompt)
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
        return linenoiseNoTTY();
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
void linenoiseFree(void* ptr)
{
    free(ptr);
}

/* ================================ History ================================= */

/* Free the history, but does not reset it. Only used when we have to
 * exit() to avoid memory leaks are reported by valgrind & co. */
void lino_freehistory(void)
{
    if(history)
    {
        int j;

        for(j = 0; j < history_len; j++)
            free(history[j]);
        free(history);
    }
}

/* At exit we'll try to fix the terminal to the initial conditions. */
void linenoiseAtExit(void)
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
int linenoiseHistoryAdd(const char* line)
{
    char* linecopy;

    if(history_max_len == 0)
        return 0;

    /* Initialization on first call. */
    if(history == NULL)
    {
        history = (char**)malloc(sizeof(char*) * history_max_len);
        if(history == NULL)
            return 0;
        memset(history, 0, (sizeof(char*) * history_max_len));
    }

    /* Don't add duplicated lines. */
    if(history_len && !strcmp(history[history_len - 1], line))
        return 0;

    /* Add an heap allocated copy of the line in the history.
   * If we reached the max length, remove the older line. */
    linecopy = strdup(line);
    if(!linecopy)
        return 0;
    if(history_len == history_max_len)
    {
        free(history[0]);
        memmove(history, history + 1, sizeof(char*) * (history_max_len - 1));
        history_len--;
    }
    history[history_len] = linecopy;
    history_len++;
    return 1;
}

/* Set the maximum length for the history. This function can be called even
 * if there is already some history, the function will make sure to retain
 * just the latest 'len' elements if the new history length value is smaller
 * than the amount of items already inside the history. */
int linenoiseHistorySetMaxLen(int len)
{
    char** newbuf;

    if(len < 1)
        return 0;
    if(history)
    {
        int tocopy = history_len;

        newbuf = (char**)malloc(sizeof(char*) * len);
        if(newbuf == NULL)
            return 0;

        /* If we can't copy everything, free the elements we'll not use. */
        if(len < tocopy)
        {
            int j;

            for(j = 0; j < tocopy - len; j++)
                free(history[j]);
            tocopy = len;
        }
        memset(newbuf, 0, sizeof(char*) * len);
        memcpy(newbuf, history + (history_len - tocopy), sizeof(char*) * tocopy);
        free(history);
        history = newbuf;
    }
    history_max_len = len;
    if(history_len > history_max_len)
        history_len = history_max_len;
    return 1;
}

/* Save the history in the specified file. On success 0 is returned
 * otherwise -1 is returned. */
int linenoiseHistorySave(const char* filename)
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
    for(j = 0; j < history_len; j++)
        fprintf(fp, "%s\n", history[j]);
    fclose(fp);
    #endif
    return 0;
}

/* Load the history from the specified file. If the file does not exist
 * zero is returned and no operation is performed.
 *
 * If the file exists and the operation succeeded 0 is returned, otherwise
 * on error -1 is returned. */
int linenoiseHistoryLoad(const char* filename)
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
        linenoiseHistoryAdd(buf);
    }
    fclose(fp);
    return 0;
}
