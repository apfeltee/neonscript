
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
 * - History search like Ctrl+r in readLine?
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
 * When utilClearScreen() is called, two additional escape sequences
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


/* Debugging macro. was too verbose, needs to implemented again */
#define lndebug(fmt, ...)

class LineReader;

static void linereader_callback_handleimplicitexit();

static LineReader* g_linereadercurrentcontext = NULL;

class LineReader
{
    public:
        enum
        {
            LRKEY_NULL = 0, /* NULL */
            LRKEY_CTRLA = 1, /* Ctrl+a */
            LRKEY_CTRLB = 2, /* Ctrl-b */
            LRKEY_CTRLC = 3, /* Ctrl-c */
            LRKEY_CTRLD = 4, /* Ctrl-d */
            LRKEY_CTRLE = 5, /* Ctrl-e */
            LRKEY_CTRLF = 6, /* Ctrl-f */
            LRKEY_CTRLH = 8, /* Ctrl-h */
            LRKEY_TAB = 9, /* Tab */
            LRKEY_LINEFEED = 10, /* Line feed */
            LRKEY_CTRLK = 11, /* Ctrl+k */
            LRKEY_CTRLL = 12, /* Ctrl+l */
            LRKEY_ENTER = 13, /* Enter */
            LRKEY_CTRLN = 14, /* Ctrl-n */
            LRKEY_CTRLP = 16, /* Ctrl-p */
            LRKEY_CTRLT = 20, /* Ctrl-t */
            LRKEY_CTRLU = 21, /* Ctrl+u */
            LRKEY_CTRLW = 23, /* Ctrl+w */
            LRKEY_ESC = 27, /* Escape */
            LRKEY_BACKSPACE = 127 /* Backspace */
        };

        /* The EditState structure represents the state during line editing.
         * We pass this state to functions implementing specific editing
         * functionalities. */
        class EditState
        {
            public:
                int m_termstdinfd; /* Terminal stdin file descriptor. */
                int m_termstdoutfd; /* Terminal stdout file descriptor. */
                char* m_edlinebuf; /* Edited line buffer. */
                size_t m_edlinelen; /* Edited line buffer size. */
                const char* m_promptdata; /* Prompt to display. */
                size_t m_promptlen; /* Prompt length. */
                size_t m_currentcursorpos; /* Current cursor position. */
                size_t m_prevrefreshcursorpos; /* Previous refresh cursor column position. */
                size_t m_currentedlinelen; /* Current edited line length. */
                size_t m_terminalcolumns; /* Number of columns in terminal. */
                size_t m_maxrowsused; /* Maximum num of rows used so far (multiline mode) */
                int m_historyindex; /* The history index we are currently editing. */
        };

        class Completions
        {
            public:
                size_t m_count;
                char** m_cvec;

            public:
                /* Free a list of completion option populated by addCompletion(). */
                void destroy()
                {
                    size_t i;
                    for(i = 0; i < m_count; i++)
                    {
                        free(m_cvec[i]);
                    }
                    if(m_cvec != NULL)
                    {
                        free(m_cvec);
                    }
                }

                /* This function is used by the callback function registered by the user
                 * in order to add completion options given the input string when the
                 * user typed <tab>. See the example.c source code for a very easy to
                 * understand example. */
                void addCompletion(const char* str)
                {
                    size_t len;
                    char *copy;
                    char** cvec;
                    (void)this;
                    len = strlen(str);
                    copy = (char*)malloc(len + 1);
                    if(copy == NULL)
                    {
                        return;
                    }
                    memcpy(copy, str, len + 1);
                    cvec = (char**)realloc(m_cvec, sizeof(char*) * (m_count + 1));
                    if(cvec == NULL)
                    {
                        free(copy);
                        return;
                    }
                    m_cvec = cvec;
                    m_cvec[m_count++] = copy;
                }
        };

        /*
        * We define a very simple "append buffer" structure, that is an heap
        * allocated string where we can append to. This is useful in order to
        * write all the escape sequences in a buffer and flush them to the standard
        * output in a single call, to avoid flickering effects.
        */
        class SimpleBuffer
        {
            public:
                char* m_bufdata = nullptr;
                int m_buflen = 0;

            public:
                SimpleBuffer()
                {
                }

                void reset()
                {
                    m_bufdata = NULL;
                    m_buflen = 0;
                }

                void append(const char* s, int len)
                {
                    char* newbuf;
                    newbuf = (char*)realloc(m_bufdata, m_buflen + len);
                    if(newbuf == NULL)
                    {
                        return;
                    }
                    memcpy(newbuf + m_buflen, s, len);
                    m_bufdata = newbuf;
                    m_buflen += len;
                }

                void destroy()
                {
                    free(m_bufdata);
                }
        };

        using CallbackCompletionFN = void(*)(LineReader*, const char *, Completions *);
        using CallbackHintFN = char*(*)(LineReader*, const char *, int *color, int *bold);
        using CallbackHintFreeFN = void(*)(LineReader*, void *);
        using CallbackPrevLenFN = size_t(*)(LineReader*, const char *buf, size_t buf_len, size_t pos, size_t *col_len);
        using CallbackNextLenFN = size_t (*)(LineReader*, const char *buf, size_t buf_len, size_t pos, size_t *col_len);
        using CallbackReadCodeFN = size_t (*)(LineReader*, int fd, char *buf, size_t buf_len, int* c);

    public:
        static int utilStrCaseCmp(const char* s1, const char* s2)
        {
            const char *cm;
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

        /* Return true if the terminal name is in the list of terminals we know are
         * not able to understand basic escape sequences. */
        static int utilIsUnsupportedTerm(void)
        {
            int j;
            char* term;
            static const char* g_linoconst_unsupportedterminals[] = { "dumb", "cons25", "emacs", NULL };
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
                if(!utilStrCaseCmp(term, g_linoconst_unsupportedterminals[j]))
                {
                    return 1;
                }
            }
            return 0;
        }

        /* Use the ESC [6n escape sequence to query the horizontal cursor position
         * and return it. On error -1 is returned, on success the position of the
         * cursor. */
        static int utilGetCursorPosition(int ifd, int ofd)
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
            if(buf[0] != LRKEY_ESC || buf[1] != '[')
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
        static int utilGetColumns(int ifd, int ofd)
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
                start = utilGetCursorPosition(ifd, ofd);
                if(start == -1)
                {
                    goto failed;
                }
                /* Go to right margin and get position. */
                if(write(ofd, "\x1b[999C", 6) != 6)
                {
                    goto failed;
                }
                cols = utilGetCursorPosition(ifd, ofd);
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
        static void utilClearScreen(void)
        {
            if(write(fileno(stdout), "\x1b[H\x1b[2J", 7) <= 0)
            {
                /* nothing to do, just to avoid warning. */
            }
        }

        /* Beep, used for completion when there is nothing to complete or when all
         * the choices were already shown. */
        static void utilBeep(void)
        {
            fprintf(stderr, "\x7");
            fflush(stderr);
        }

        /* Check if text is an ANSI escape sequence
         */
        static int utilIsANSIEscape(const char* buf, size_t buflen, size_t* len)
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

        /* Read bytes of the next character */
        static size_t defaultReadcode(LineReader* ctx, int fd, char* buf, size_t buflen, int* c)
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

        /* Get byte length and column length of the next character */
        static size_t defaultNextCharLen(LineReader* ctx, const char* buf, size_t buflen, size_t pos, size_t* collen)
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

        /* Get byte length and column length of the previous character */
        static size_t defaultPrevCharLen(LineReader* ctx, const char* buf, size_t buflen, size_t pos, size_t* collen)
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

    public:
        CallbackCompletionFN m_completioncallback;
        CallbackHintFN m_hintscallback;
        CallbackHintFreeFN m_freehintscallback;

        /* Set default encoding functions */
        CallbackPrevLenFN m_fnprevcharlen;
        CallbackNextLenFN m_fnnextcharlen;
        CallbackReadCodeFN m_fnreadcode;

        #if defined(LINENOISE_ISUNIX)
        struct termios m_origtermios; /* In order to restore at exit.*/
        #endif
        int m_maskmode; /* Show "***" instead of input. For passwords. */
        int m_israwmode; /* For atexit() function to check if restore is needed*/
        int m_ismultilinemode; /* Multi line mode. Default is single line. */
        int m_atexitregistered; /* Register atexit just 1 time. */
        int m_historymaxlen;
        int m_historylength;
        char** m_historybuflines;

    public:
        LineReader()
        {
            m_completioncallback = NULL;
            m_hintscallback = NULL;
            m_freehintscallback = NULL;
            m_maskmode = 0; /* Show "***" instead of input. For passwords. */
            m_israwmode = 0; /* For atexit() function to check if restore is needed*/
            m_ismultilinemode = 0; /* Multi line mode. Default is single line. */
            m_atexitregistered = 0; /* Register atexit just 1 time. */
            m_historymaxlen = LINENOISE_DEFAULT_HISTORY_MAX_LEN;
            m_historylength = 0;
            m_historybuflines = NULL;
            /* Set default encoding functions */
            m_fnprevcharlen = defaultPrevCharLen;
            m_fnnextcharlen = defaultNextCharLen;
            m_fnreadcode = defaultReadcode;
            g_linereadercurrentcontext = this;
        }


        /* Set used defined encoding functions */
        void setEncodingFunctions(CallbackPrevLenFN pclfunc, CallbackNextLenFN nclfunc, CallbackReadCodeFN rcfunc)
        {
            m_fnprevcharlen = pclfunc;
            m_fnnextcharlen = nclfunc;
            m_fnreadcode = rcfunc;
        }

        /* Get column length from begining of buffer to current byte position */
        size_t getColumnPos(const char* buf, size_t buflen, size_t pos)
        {
            size_t ret;
            size_t off;
            size_t len;
            size_t collen;
            ret = 0;
            off = 0;
            while(off < pos)
            {
                len = m_fnnextcharlen(this, buf, buflen, off, &collen);
                off += len;
                ret += collen;
            }
            return ret;
        }

        /* Get column length from begining of buffer to current byte position for multiline mode*/
        size_t getColumnposForMultiline(const char* buf, size_t buflen, size_t pos, size_t cols, size_t ini_pos)
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
                len = m_fnnextcharlen(this, buf, buflen, off, &collen);
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
        void maskModeEnable()
        {
            m_maskmode = 1;
        }

        /* Disable mask mode. */
        void maskModeDisable()
        {
            m_maskmode = 0;
        }

        /* Set if to use or not the multi line mode. */
        void setMultiline(int ml)
        {
            m_ismultilinemode = ml;
        }

        /* Raw mode: 1960 magic shit. */
        int enableRawModeFor(int fd)
        {
            #if defined(LINENOISE_ISUNIX)
            struct termios raw;
            if(!isatty(fileno(stdin)))
            {
                goto fatal;
            }
            if(!m_atexitregistered)
            {
                atexit(linereader_callback_handleimplicitexit);
                m_atexitregistered = 1;
            }
            if(tcgetattr(fd, &m_origtermios) == -1)
            {
                goto fatal;
            }
            raw = m_origtermios; /* modify the original mode */
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
            m_israwmode = 1;
            return 0;
        fatal:
            errno = ENOTTY;
            #endif
            return -1;
        }

        void disableRawModeFor(int fd)
        {
            #if defined(LINENOISE_ISUNIX)
            /* Don't even check the return value as it's too late. */
            if(m_israwmode && tcsetattr(fd, TCSADRAIN, &m_origtermios) != -1)
            {
                m_israwmode = 0;
            }
            #endif
        }

        /* This is an helper function for lino_edit_editline and is called when the
         * user types the <tab> key in order to complete the string currently in the
         * input.
         *
         * The state of the editing is encapsulated into the pointed EditState
         * structure as described in the structure definition. */
        int completeLine(EditState* ls, char* cbuf, size_t cbuflen, int* c)
        {
            int nread;
            int nwritten;
            size_t i;
            size_t stop;
            EditState saved;
            Completions lc = { 0, NULL };
            nread = 0;
            *c = 0;
            m_completioncallback(this, ls->m_edlinebuf, &lc);
            if(lc.m_count == 0)
            {
                utilBeep();
            }
            else
            {
                stop = 0;
                i = 0;
                while(!stop)
                {
                    /* Show completion or original buffer */
                    if(i < lc.m_count)
                    {
                        saved = *ls;
                        ls->m_currentedlinelen = ls->m_currentcursorpos = strlen(lc.m_cvec[i]);
                        ls->m_edlinebuf = lc.m_cvec[i];
                        refreshLine(ls);
                        ls->m_currentedlinelen = saved.m_currentedlinelen;
                        ls->m_currentcursorpos = saved.m_currentcursorpos;
                        ls->m_edlinebuf = saved.m_edlinebuf;
                    }
                    else
                    {
                        refreshLine(ls);
                    }
                    nread = m_fnreadcode(this, ls->m_termstdinfd, cbuf, cbuflen, c);
                    if(nread <= 0)
                    {
                        lc.destroy();
                        *c = -1;
                        return nread;
                    }
                    switch(*c)
                    {
                        case 9: /* tab */
                            {
                                i = (i + 1) % (lc.m_count + 1);
                                if(i == lc.m_count)
                                {
                                    utilBeep();
                                }
                            }
                            break;
                        case 27: /* escape */
                            {
                                /* Re-show original buffer */
                                if(i < lc.m_count)
                                {
                                    refreshLine(ls);
                                }
                                stop = 1;
                            }
                            break;
                        default:
                            /* Update buffer and return */
                            {
                                if(i < lc.m_count)
                                {
                                    nwritten = snprintf(ls->m_edlinebuf, ls->m_edlinelen, "%s", lc.m_cvec[i]);
                                    ls->m_currentedlinelen = ls->m_currentcursorpos = nwritten;
                                }
                                stop = 1;
                            }
                            break;
                    }
                }
            }
            lc.destroy();
            return nread;
        }

        /* Register a callback function to be called for tab-completion. */
        void setCompletionCallback(CallbackCompletionFN fn)
        {
            m_completioncallback = fn;
        }

        /* Register a hits function to be called to show hits to the user at the
         * right of the prompt. */
        void setHintsCallback(CallbackHintFN fn)
        {
            m_hintscallback = fn;
        }

        /* Register a function to free the hints returned by the hints callback
         * registered with setHintsCallback(). */
        void setFreeHintsCallback(CallbackHintFreeFN fn)
        {
            m_freehintscallback = fn;
        }



        /* Helper of refreshSingleLine() and refreshMultiline() to show hints
         * to the right of the prompt. */
        void refreshShowHints(SimpleBuffer* ab, EditState* edst, int pcollen)
        {
            int bold;
            int color;
            int hintlen;
            int hintmaxlen;
            size_t collen;
            char* hint;
            char seq[64];
            collen = pcollen + getColumnPos(edst->m_edlinebuf, edst->m_currentedlinelen, edst->m_currentedlinelen);
            if(m_hintscallback && collen < edst->m_terminalcolumns)
            {
                color = -1;
                bold = 0;
                hint = m_hintscallback(this, edst->m_edlinebuf, &color, &bold);
                if(hint)
                {
                    hintlen = strlen(hint);
                    hintmaxlen = edst->m_terminalcolumns - collen;
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
                    ab->append(seq, strlen(seq));
                    ab->append(hint, hintlen);
                    if(color != -1 || bold != 0)
                    {
                        ab->append("\033[0m", 4);
                    }
                    /* Call the function to free the hint returned. */
                    if(m_freehintscallback)
                    {
                        m_freehintscallback(this, hint);
                    }
                }
            }
        }


        /* Get column length of prompt text
         */
        size_t utilPrompttextcolumnlen(const char* prompt, size_t plen)
        {
            size_t off;
            size_t len;
            size_t buflen;
            char buf[LINENOISE_MAX_LINE];
            buflen = 0;
            off = 0;
            while(off < plen)
            {
                if(utilIsANSIEscape(prompt + off, plen - off, &len))
                {
                    off += len;
                    continue;
                }
                buf[buflen++] = prompt[off++];
            }
            return getColumnPos(buf, buflen, buflen);
        }

        /* Single line low level line refresh.
         *
         * Rewrite the currently edited line accordingly to the buffer content,
         * cursor position, and number of columns of the terminal. */
        void refreshSingleLine(EditState* edst)
        {
            int fd;
            int chlen;
            size_t pcollen;
            size_t len;
            size_t pos;
            char* buf;
            char seq[64];
            SimpleBuffer ab;
            pcollen = utilPrompttextcolumnlen(edst->m_promptdata, strlen(edst->m_promptdata));
            fd = edst->m_termstdoutfd;
            buf = edst->m_edlinebuf;
            len = edst->m_currentedlinelen;
            pos = edst->m_currentcursorpos;
            while((pcollen + getColumnPos(buf, len, pos)) >= edst->m_terminalcolumns)
            {
                chlen = m_fnnextcharlen(this, buf, len, 0, NULL);
                buf += chlen;
                len -= chlen;
                pos -= chlen;
            }
            while(pcollen + getColumnPos(buf, len, len) > edst->m_terminalcolumns)
            {
                len -= m_fnprevcharlen(this, buf, len, len, NULL);
            }
            ab.reset();
            /* Cursor to left edge */
            snprintf(seq, 64, "\r");
            ab.append(seq, strlen(seq));
            /* Write the prompt and the current buffer content */
            ab.append(edst->m_promptdata, strlen(edst->m_promptdata));
            if(m_maskmode == 1)
            {
                while(len--)
                {
                    ab.append("*", 1);
                }
            }
            else
            {
                ab.append(buf, len);
            }
            /* Show hits if any. */
            refreshShowHints(&ab, edst, pcollen);
            /* Erase to right */
            snprintf(seq, 64, "\x1b[0K");
            ab.append(seq, strlen(seq));
            /* Move cursor to original position. */
            snprintf(seq, 64, "\r\x1b[%dC", (int)(getColumnPos(buf, len, pos) + pcollen));
            ab.append(seq, strlen(seq));
            if(write(fd, ab.m_bufdata, ab.m_buflen) == -1)
            {
            } /* Can't recover from write error. */
            ab.destroy();
        }

        /* Multi line low level line refresh.
         *
         * Rewrite the currently edited line accordingly to the buffer content,
         * cursor position, and number of columns of the terminal. */
        void refreshMultiline(EditState* edst)
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
            SimpleBuffer ab;
            pcollen = utilPrompttextcolumnlen(edst->m_promptdata, strlen(edst->m_promptdata));
            colpos = getColumnposForMultiline(edst->m_edlinebuf, edst->m_currentedlinelen, edst->m_currentedlinelen, edst->m_terminalcolumns, pcollen);
            rows = (pcollen + colpos + edst->m_terminalcolumns - 1) / edst->m_terminalcolumns; /* rows used by current buf. */
            rpos = (pcollen + edst->m_prevrefreshcursorpos + edst->m_terminalcolumns) / edst->m_terminalcolumns; /* cursor relative row. */
            oldrows = edst->m_maxrowsused;
            fd = edst->m_termstdoutfd;
            /* Update m_maxrowsused if needed. */
            if(rows > (int)edst->m_maxrowsused)
            {
                edst->m_maxrowsused = rows;
            }
            /* First step: clear all the lines used before. To do so start by
           * going to the last row. */
            ab.reset();
            if(oldrows - rpos > 0)
            {
                lndebug("go down %d", oldrows - rpos);
                snprintf(seq, 64, "\x1b[%dB", oldrows - rpos);
                ab.append(seq, strlen(seq));
            }
            /* Now for every row clear it, go up. */
            for(j = 0; j < oldrows - 1; j++)
            {
                lndebug("clear+up");
                snprintf(seq, 64, "\r\x1b[0K\x1b[1A");
                ab.append(seq, strlen(seq));
            }
            /* Clean the top line. */
            lndebug("clear");
            snprintf(seq, 64, "\r\x1b[0K");
            ab.append(seq, strlen(seq));
            /* Write the prompt and the current buffer content */
            ab.append(edst->m_promptdata, strlen(edst->m_promptdata));
            if(m_maskmode == 1)
            {
                for(i = 0; i < edst->m_currentedlinelen; i++)
                {
                    ab.append("*", 1);
                }
            }
            else
            {
                ab.append(edst->m_edlinebuf, edst->m_currentedlinelen);
            }
            /* Show hits if any. */
            refreshShowHints(&ab, edst, pcollen);
            /* Get column length to cursor position */
            colpos2 = getColumnposForMultiline(edst->m_edlinebuf, edst->m_currentedlinelen, edst->m_currentcursorpos, edst->m_terminalcolumns, pcollen);
            /* If we are at the very end of the screen with our prompt, we need to
           * emit a newline and move the prompt to the first column. */
            if(edst->m_currentcursorpos && edst->m_currentcursorpos == edst->m_currentedlinelen && (colpos2 + pcollen) % edst->m_terminalcolumns == 0)
            {
                lndebug("<newline>");
                ab.append("\n", 1);
                snprintf(seq, 64, "\r");
                ab.append(seq, strlen(seq));
                rows++;
                if(rows > (int)edst->m_maxrowsused)
                {
                    edst->m_maxrowsused = rows;
                }
            }
            /* Move cursor to right position. */
            rpos2 = (pcollen + colpos2 + edst->m_terminalcolumns) / edst->m_terminalcolumns; /* current cursor relative row. */
            lndebug("rpos2 %d", rpos2);
            /* Go up till we reach the expected positon. */
            if(rows - rpos2 > 0)
            {
                lndebug("go-up %d", rows - rpos2);
                snprintf(seq, 64, "\x1b[%dA", rows - rpos2);
                ab.append(seq, strlen(seq));
            }
            /* Set column. */
            col = (pcollen + colpos2) % edst->m_terminalcolumns;
            lndebug("set col %d", 1 + col);
            if(col)
            {
                snprintf(seq, 64, "\r\x1b[%dC", col);
            }
            else
            {
                snprintf(seq, 64, "\r");
            }
            ab.append(seq, strlen(seq));
            lndebug("\n");
            edst->m_prevrefreshcursorpos = colpos2;
            if(write(fd, ab.m_bufdata, ab.m_buflen) == -1)
            {
            } /* Can't recover from write error. */
            ab.destroy();
        }

        /* Calls the two low level functions refreshSingleLine() or
         * refreshMultiline() according to the selected mode. */
        void refreshLine(EditState* edst)
        {
            if(m_ismultilinemode)
            {
                refreshMultiline(edst);
            }
            else
            {
                refreshSingleLine(edst);
            }
        }

        /* Insert the character 'c' at cursor current position.
         *
         * On error writing to the terminal -1 is returned, otherwise 0. */
        int doInsert(EditState* edst, const char* cbuf, int clen)
        {
            if(edst->m_currentedlinelen + clen <= edst->m_edlinelen)
            {
                if(edst->m_currentedlinelen == edst->m_currentcursorpos)
                {
                    memcpy(&edst->m_edlinebuf[edst->m_currentcursorpos], cbuf, clen);
                    edst->m_currentcursorpos += clen;
                    edst->m_currentedlinelen += clen;
                    edst->m_edlinebuf[edst->m_currentedlinelen] = '\0';
                    if((!m_ismultilinemode && utilPrompttextcolumnlen(edst->m_promptdata, edst->m_promptlen) + getColumnPos(edst->m_edlinebuf, edst->m_currentedlinelen, edst->m_currentedlinelen) < edst->m_terminalcolumns && !m_hintscallback))
                    {
                        /* Avoid a full update of the line in the trivial case. */
                        if(m_maskmode == 1)
                        {
                            static const char d = '*';
                            if(write(edst->m_termstdoutfd, &d, 1) == -1)
                            {
                                return -1;
                            }
                        }
                        else
                        {
                            if(write(edst->m_termstdoutfd, cbuf, clen) == -1)
                            {
                                return -1;
                            }
                        }
                    }
                    else
                    {
                        refreshLine(edst);
                    }
                }
                else
                {
                    memmove(edst->m_edlinebuf + edst->m_currentcursorpos + clen, edst->m_edlinebuf + edst->m_currentcursorpos, edst->m_currentedlinelen - edst->m_currentcursorpos);
                    memcpy(&edst->m_edlinebuf[edst->m_currentcursorpos], cbuf, clen);
                    edst->m_currentcursorpos += clen;
                    edst->m_currentedlinelen += clen;
                    edst->m_edlinebuf[edst->m_currentedlinelen] = '\0';
                    refreshLine(edst);
                }
            }
            return 0;
        }

        /* Move cursor on the left. */
        void doMoveleft(EditState* edst)
        {
            if(edst->m_currentcursorpos > 0)
            {
                edst->m_currentcursorpos -= m_fnprevcharlen(this, edst->m_edlinebuf, edst->m_currentedlinelen, edst->m_currentcursorpos, NULL);
                refreshLine(edst);
            }
        }

        /* Move cursor on the right. */
        void doMoveright(EditState* edst)
        {
            if(edst->m_currentcursorpos != edst->m_currentedlinelen)
            {
                edst->m_currentcursorpos += m_fnnextcharlen(this, edst->m_edlinebuf, edst->m_currentedlinelen, edst->m_currentcursorpos, NULL);
                refreshLine(edst);
            }
        }

        /* Move cursor to the start of the line. */
        void doMovehome(EditState* edst)
        {
            if(edst->m_currentcursorpos != 0)
            {
                edst->m_currentcursorpos = 0;
                refreshLine(edst);
            }
        }

        /* Move cursor to the end of the line. */
        void doMoveend(EditState* edst)
        {
            if(edst->m_currentcursorpos != edst->m_currentedlinelen)
            {
                edst->m_currentcursorpos = edst->m_currentedlinelen;
                refreshLine(edst);
            }
        }

        /* Substitute the currently edited line with the next or previous history
         * entry as specified by 'dir'. */
        #define LINENOISE_HISTORY_NEXT 0
        #define LINENOISE_HISTORY_PREV 1

        void doHistorynext(EditState* edst, int dir)
        {
            int ipos;
            if(m_historylength > 1)
            {
                /* Update the current history entry before to overwrite it with the next one. */
                ipos = m_historylength - 1 - edst->m_historyindex;
                free(m_historybuflines[ipos]);
                m_historybuflines[ipos] = strdup(edst->m_edlinebuf);
                /* Show the new entry */
                edst->m_historyindex += (dir == LINENOISE_HISTORY_PREV) ? 1 : -1;
                if(edst->m_historyindex < 0)
                {
                    edst->m_historyindex = 0;
                    return;
                }
                else if(edst->m_historyindex >= m_historylength)
                {
                    edst->m_historyindex = m_historylength - 1;
                    return;
                }
                strncpy(edst->m_edlinebuf, m_historybuflines[m_historylength - 1 - edst->m_historyindex], edst->m_edlinelen);
                edst->m_edlinebuf[edst->m_edlinelen - 1] = '\0';
                edst->m_currentedlinelen = edst->m_currentcursorpos = strlen(edst->m_edlinebuf);
                refreshLine(edst);
            }
        }

        /* Delete the character at the right of the cursor without altering the cursor
         * position. Basically this is what happens with the "Delete" keyboard key. */
        void doDelete(EditState* edst)
        {
            int chlen;
            if(edst->m_currentedlinelen > 0 && edst->m_currentcursorpos < edst->m_currentedlinelen)
            {
                chlen = m_fnnextcharlen(this, edst->m_edlinebuf, edst->m_currentedlinelen, edst->m_currentcursorpos, NULL);
                memmove(edst->m_edlinebuf + edst->m_currentcursorpos, edst->m_edlinebuf + edst->m_currentcursorpos + chlen, edst->m_currentedlinelen - edst->m_currentcursorpos - chlen);
                edst->m_currentedlinelen -= chlen;
                edst->m_edlinebuf[edst->m_currentedlinelen] = '\0';
                refreshLine(edst);
            }
        }

        /* Backspace implementation. */
        void doBackspace(EditState* edst)
        {
            int chlen;
            if(edst->m_currentcursorpos > 0 && edst->m_currentedlinelen > 0)
            {
                chlen = m_fnprevcharlen(this, edst->m_edlinebuf, edst->m_currentedlinelen, edst->m_currentcursorpos, NULL);
                memmove(edst->m_edlinebuf + edst->m_currentcursorpos - chlen, edst->m_edlinebuf + edst->m_currentcursorpos, edst->m_currentedlinelen - edst->m_currentcursorpos);
                edst->m_currentcursorpos -= chlen;
                edst->m_currentedlinelen -= chlen;
                edst->m_edlinebuf[edst->m_currentedlinelen] = '\0';
                refreshLine(edst);
            }
        }

        /* Delete the previosu word, maintaining the cursor at the start of the
         * current word. */
        void doDelprevword(EditState* edst)
        {
            size_t diff;
            size_t oldpos;
            oldpos = edst->m_currentcursorpos;
            while(edst->m_currentcursorpos > 0 && edst->m_edlinebuf[edst->m_currentcursorpos - 1] == ' ')
            {
                edst->m_currentcursorpos--;
            }
            while(edst->m_currentcursorpos > 0 && edst->m_edlinebuf[edst->m_currentcursorpos - 1] != ' ')
            {
                edst->m_currentcursorpos--;
            }
            diff = oldpos - edst->m_currentcursorpos;
            memmove(edst->m_edlinebuf + edst->m_currentcursorpos, edst->m_edlinebuf + oldpos, edst->m_currentedlinelen - oldpos + 1);
            edst->m_currentedlinelen -= diff;
            refreshLine(edst);
        }

        /* This function is the core of the line editing capability of linenoise.
         * It expects 'fd' to be already in "raw mode" so that every key pressed
         * will be returned ASAP to read().
         *
         * The resulting string is put into 'buf' when the user type enter, or
         * when ctrl+d is typed.
         *
         * The function returns the length of the current buffer. */
        int doEditline(int stdin_fd, int stdout_fd, char* buf, size_t buflen, const char* prompt)
        {
            int c;
            int aux;
            int nread;
            char seq[3];
            char cbuf[32];/* large enough for any encoding? */
            EditState edst;
            /* Populate the linenoise state that we pass to functions implementing
           * specific editing functionalities. */
            edst.m_termstdinfd = stdin_fd;
            edst.m_termstdoutfd = stdout_fd;
            edst.m_edlinebuf = buf;
            edst.m_edlinelen = buflen;
            edst.m_promptdata = prompt;
            edst.m_promptlen = strlen(prompt);
            edst.m_prevrefreshcursorpos = edst.m_currentcursorpos = 0;
            edst.m_currentedlinelen = 0;
            edst.m_terminalcolumns = utilGetColumns(stdin_fd, stdout_fd);
            edst.m_maxrowsused = 0;
            edst.m_historyindex = 0;
            /* Buffer starts empty. */
            edst.m_edlinebuf[0] = '\0';
            edst.m_edlinelen--; /* Make sure there is always space for the nulterm */
            /* The latest history entry is always our current buffer, that initially is just an empty string. */
            this->historyAdd("");
            if(write(edst.m_termstdoutfd, prompt, edst.m_promptlen) == -1)
            {
                return -1;
            }
            while(1)
            {
                nread = m_fnreadcode(this, edst.m_termstdinfd, cbuf, sizeof(cbuf), &c);
                if(nread <= 0)
                {
                    return edst.m_currentedlinelen;
                }
                /*
                * Only autocomplete when the callback is set. It returns < 0 when
                * there was an error reading from fd. Otherwise it will return the
                * character that should be handled next.
                */
                if(c == 9 && m_completioncallback != NULL)
                {
                    nread = this->completeLine(&edst, cbuf, sizeof(cbuf), &c);
                    /* Return on errors */
                    if(c < 0)
                    {
                        return edst.m_currentedlinelen;
                    }
                    /* Read next character when 0 */
                    if(c == 0)
                    {
                        continue;
                    }
                }
                switch(c)
                {
                    case LRKEY_LINEFEED:
                    case LRKEY_ENTER: /* enter */
                        {
                            CallbackHintFN hc;
                            m_historylength--;
                            free(m_historybuflines[m_historylength]);
                            m_historybuflines[m_historylength] = NULL;
                            if(m_ismultilinemode)
                            {
                                this->doMoveend(&edst);
                            }
                            if(m_hintscallback)
                            {
                                /* Force a refresh without hints to leave the previous line as the user typed it after a newline. */
                                hc = m_hintscallback;
                                m_hintscallback = NULL;
                                this->refreshLine(&edst);
                                m_hintscallback = hc;
                            }
                            return (int)edst.m_currentedlinelen;
                        }
                        break;
                    case LRKEY_CTRLC: /* ctrl-c, clear line */
                        {
                            buf[0] = '\0';
                            edst.m_currentcursorpos = edst.m_currentedlinelen = 0;
                            this->refreshLine(&edst);
                        }
                        break;
                    case LRKEY_BACKSPACE: /* backspace */
                    case 8: /* ctrl-h */
                        {
                            this->doBackspace(&edst);
                        }
                        break;
                    case LRKEY_CTRLD: /* ctrl-d, act as end-of-file. */
                        {
                            m_historylength--;
                            free(m_historybuflines[m_historylength]);
                            m_historybuflines[m_historylength] = NULL;
                            return -1;
                        }
                        break;
                    case LRKEY_CTRLT: /* ctrl-t, swaps current character with previous. */
                        {
                            if(edst.m_currentcursorpos > 0 && edst.m_currentcursorpos < edst.m_currentedlinelen)
                            {
                                aux = buf[edst.m_currentcursorpos - 1];
                                buf[edst.m_currentcursorpos - 1] = buf[edst.m_currentcursorpos];
                                buf[edst.m_currentcursorpos] = aux;
                                if(edst.m_currentcursorpos != edst.m_currentedlinelen - 1)
                                {
                                    edst.m_currentcursorpos++;
                                }
                                this->refreshLine(&edst);
                            }
                        }
                        break;
                    case LRKEY_CTRLB: /* ctrl-b */
                        {
                            this->doMoveleft(&edst);
                        }
                        break;
                    case LRKEY_CTRLF: /* ctrl-f */
                        {
                            this->doMoveright(&edst);
                        }
                        break;
                    case LRKEY_CTRLP: /* ctrl-p */
                        {
                            this->doHistorynext(&edst, LINENOISE_HISTORY_PREV);
                        }
                        break;
                    case LRKEY_CTRLN: /* ctrl-n */
                        {
                            this->doHistorynext(&edst, LINENOISE_HISTORY_NEXT);
                        }
                        break;
                    case LRKEY_ESC: /* escape sequence */
                        {
                            /*
                            * Read the next two bytes representing the escape sequence.
                            * Use two calls to handle slow terminals returning the two
                            * chars at different times.
                            */
                            if(read(edst.m_termstdinfd, seq, 1) == -1)
                            {
                                break;
                            }
                            if(read(edst.m_termstdinfd, seq + 1, 1) == -1)
                            {
                                break;
                            }
                            /* ESC [ sequences. */
                            if(seq[0] == '[')
                            {
                                if(seq[1] >= '0' && seq[1] <= '9')
                                {
                                    /* Extended escape, read additional byte. */
                                    if(read(edst.m_termstdinfd, seq + 2, 1) == -1)
                                    {
                                        break;
                                    }
                                    if(seq[2] == '~')
                                    {
                                        switch(seq[1])
                                        {
                                            case '3': /* Delete key. */
                                                {
                                                    this->doDelete(&edst);
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
                                                this->doHistorynext(&edst, LINENOISE_HISTORY_PREV);
                                            }
                                            break;
                                        case 'B': /* Down */
                                            {
                                                this->doHistorynext(&edst, LINENOISE_HISTORY_NEXT);
                                            }
                                            break;
                                        case 'C': /* Right */
                                            {
                                                this->doMoveright(&edst);
                                            }
                                            break;
                                        case 'D': /* Left */
                                            {
                                                this->doMoveleft(&edst);
                                            }
                                            break;
                                        case 'H': /* Home */
                                            {
                                                this->doMovehome(&edst);
                                            }
                                            break;
                                        case 'F': /* End*/
                                            {
                                                this->doMoveend(&edst);
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
                                            this->doMovehome(&edst);
                                        }
                                        break;
                                    case 'F': /* End*/
                                        {
                                            this->doMoveend(&edst);
                                        }
                                        break;
                                }
                            }
                        }
                        break;
                    default:
                        {
                            if(this->doInsert(&edst, cbuf, nread))
                            {
                                return -1;
                            }
                        }
                        break;
                    case LRKEY_CTRLU: /* Ctrl+u, delete the whole line. */
                        {
                            buf[0] = '\0';
                            edst.m_currentcursorpos = edst.m_currentedlinelen = 0;
                            this->refreshLine(&edst);
                        }
                        break;
                    case LRKEY_CTRLK: /* Ctrl+k, delete from current to end of line. */
                        {
                            buf[edst.m_currentcursorpos] = '\0';
                            edst.m_currentedlinelen = edst.m_currentcursorpos;
                            this->refreshLine(&edst);
                        }
                        break;
                    case LRKEY_CTRLA: /* Ctrl+a, go to the start of the line */
                        {
                            this->doMovehome(&edst);
                        }
                        break;
                    case LRKEY_CTRLE: /* ctrl+e, go to the end of the line */
                        {
                            this->doMoveend(&edst);
                        }
                        break;
                    case LRKEY_CTRLL: /* ctrl+edst, clear screen */
                        {
                            utilClearScreen();
                            this->refreshLine(&edst);
                        }
                        break;
                    case LRKEY_CTRLW: /* ctrl+w, delete previous word */
                        {
                            this->doDelprevword(&edst);
                        }
                        break;
                }
            }
            return edst.m_currentedlinelen;
        }

        /* This special mode is used by linenoise in order to print scan codes
         * on screen for debugging / development purposes. It is implemented
         * by the linenoise_example program using the --keycodes option. */
        void debugPrintKeycodes()
        {
            int nread;
            char c;
            char quit[4];

            printf(
                "Linenoise key codes debugging mode.\n"
                "Press keys to see scan codes. Type 'quit' at any time to exit.\n"
            );
            if(this->enableRawModeFor(fileno(stdin)) == -1)
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
            this->disableRawModeFor(fileno(stdin));
        }

        /* This function calls the line editing function lino_edit_editline using
         * the STDIN file descriptor set in raw mode. */
        int getRaw(char* buf, size_t buflen, const char* prompt)
        {
            int count;
            if(buflen == 0)
            {
                errno = EINVAL;
                return -1;
            }
            #if defined(LINENOISE_ISUNIX)
            if(this->enableRawModeFor(fileno(stdin)) == -1)
            {
                return -1;
            }
            #endif
            count = this->doEditline(fileno(stdin), fileno(stdout), buf, buflen, prompt);
            this->disableRawModeFor(fileno(stdin));
            printf("\n");
            return count;
        }

        /* This function is called when linenoise() is called with the standard
         * input file descriptor not attached to a TTY. So for example when the
         * program using linenoise is called in pipe or with a file redirected
         * to its standard input. In this case, we want to be able to return the
         * line regardless of its length (by default we are limited to 4k). */
        char* fallbackNoTTY()
        {
            int ch;
            size_t len;
            size_t maxlen;
            char* line;
            char* oldval;
            (void)this;
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
        char* readLine(const char* prompt)
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
                return fallbackNoTTY();
                #endif
            }
            else if(utilIsUnsupportedTerm())
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
            count = this->getRaw(buf, LINENOISE_MAX_LINE, prompt);
            if(count == -1)
            {
                return NULL;
            }
            return strdup(buf);
        }


        /* ================================ History ================================= */

        /* Free the history, but does not reset it. Only used when we have to
         * exit() to avoid memory leaks are reported by valgrind & co. */
        void freeHistory()
        {
            int j;
            if(m_historybuflines != NULL)
            {
                for(j = 0; j < m_historylength; j++)
                {
                    if(m_historybuflines[j] != NULL)
                    {
                        free(m_historybuflines[j]);
                        m_historybuflines[j] = NULL;
                    }
                }
                free(m_historybuflines);
                m_historybuflines = NULL;
            }
        }


        /* This is the API call to add a new entry in the linenoise history.
         * It uses a fixed array of char pointers that are shifted (memmoved)
         * when the history max length is reached in order to remove the older
         * entry and make room for the new one, so it is not exactly suitable for huge
         * histories, but will work well for a few hundred of entries.
         *
         * Using a circular buffer is smarter, but a bit more complex to handle. */
        int historyAdd(const char* line)
        {
            char* linecopy;
            if(m_historymaxlen == 0)
            {
                return 0;
            }
            /* Initialization on first call. */
            if(m_historybuflines == NULL)
            {
                m_historybuflines = (char**)malloc(sizeof(char*) * m_historymaxlen);
                assert(m_historybuflines != NULL);
                if(m_historybuflines == NULL)
                {
                    return 0;
                }
                memset(m_historybuflines, 0, (sizeof(char*) * m_historymaxlen));
            }
            /* Don't add duplicated lines. */
            if(m_historylength && !strcmp(m_historybuflines[m_historylength - 1], line))
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
            if(m_historylength == m_historymaxlen)
            {
                free(m_historybuflines[0]);
                m_historybuflines[0] = NULL;
                memmove(m_historybuflines, m_historybuflines + 1, sizeof(char*) * (m_historymaxlen - 1));
                m_historylength--;
            }
            m_historybuflines[m_historylength] = linecopy;
            m_historylength++;
            return 1;
        }

        /* Set the maximum length for the history. This function can be called even
         * if there is already some history, the function will make sure to retain
         * just the latest 'len' elements if the new history length value is smaller
         * than the amount of items already inside the history. */
        int historySetMaxLength(int len)
        {
            int j;
            int tocopy;
            char** newbuf;
            if(len < 1)
            {
                return 0;
            }
            if(m_historybuflines)
            {
                tocopy = m_historylength;
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
                        free(m_historybuflines[j]);
                        m_historybuflines[j] = NULL;
                    }
                    tocopy = len;
                }
                memset(newbuf, 0, sizeof(char*) * len);
                memcpy(newbuf, m_historybuflines + (m_historylength - tocopy), sizeof(char*) * tocopy);
                free(m_historybuflines);
                m_historybuflines = newbuf;
            }
            m_historymaxlen = len;
            if(m_historylength > m_historymaxlen)
            {
                m_historylength = m_historymaxlen;
            }
            return 1;
        }

        /* Save the history in the specified file. On success 0 is returned
         * otherwise -1 is returned. */
        int historySaveToFile(const char* filename)
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
            for(j = 0; j < m_historylength; j++)
            {
                fprintf(fp, "%s\n", m_historybuflines[j]);
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
        int historyLoadFromFile(const char* filename)
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
                this->historyAdd(buf);
            }
            fclose(fp);
            return 0;
        }

        /* This is just a wrapper the user may want to call in order to make sure
         * the linenoise returned buffer is freed with the same allocator it was
         * created with. Useful when the main program is using an alternative
         * allocator. */
        void freeLine(void* ptr)
        {
            free(ptr);
        }
};

/* At exit we'll try to fix the terminal to the initial conditions. */
static void linereader_callback_handleimplicitexit()
{
    g_linereadercurrentcontext->disableRawModeFor(fileno(stdin));
    g_linereadercurrentcontext->freeHistory();
}

#endif
