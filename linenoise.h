
#pragma once
/* linenoise.h -- VERSION 1.0
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

typedef struct LNAppendBuffer LNAppendBuffer;
typedef struct linenoiseCompletions linenoiseCompletions;
typedef struct linenoiseState linenoiseState; 
typedef void(linenoiseCompletionCallback)(const char *, linenoiseCompletions *);
typedef char*(linenoiseHintsCallback)(const char *, int *color, int *bold);
typedef void(linenoiseFreeHintsCallback)(void *);

typedef size_t (linenoisePrevCharLen)(const char *buf, size_t buf_len, size_t pos, size_t *col_len);
typedef size_t (linenoiseNextCharLen)(const char *buf, size_t buf_len, size_t pos, size_t *col_len);
typedef size_t (linenoiseReadCode)(int fd, char *buf, size_t buf_len, int* c);


struct LNAppendBuffer
{
    char* b;
    int len;
};


/* The linenoiseState structure represents the state during line editing.
 * We pass this state to functions implementing specific editing
 * functionalities. */
struct linenoiseState
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

enum KEY_ACTION
{
    LINENOISE_KEY_NULL = 0, /* NULL */
    LINENOISE_KEY_CTRLA = 1, /* Ctrl+a */
    LINENOISE_KEY_CTRLB = 2, /* Ctrl-b */
    LINENOISE_KEY_CTRLC = 3, /* Ctrl-c */
    LINENOISE_KEY_CTRLD = 4, /* Ctrl-d */
    LINENOISE_KEY_CTRLE = 5, /* Ctrl-e */
    LINENOISE_KEY_CTRLF = 6, /* Ctrl-f */
    LINENOISE_KEY_CTRLH = 8, /* Ctrl-h */
    LINENOISE_KEY_TAB = 9, /* Tab */
    LINENOISE_KEY_LINEFEED = 10, /* Line feed */
    LINENOISE_KEY_CTRLK = 11, /* Ctrl+k */
    LINENOISE_KEY_CTRLL = 12, /* Ctrl+l */
    LINENOISE_KEY_ENTER = 13, /* Enter */
    LINENOISE_KEY_CTRLN = 14, /* Ctrl-n */
    LINENOISE_KEY_CTRLP = 16, /* Ctrl-p */
    LINENOISE_KEY_CTRLT = 20, /* Ctrl-t */
    LINENOISE_KEY_CTRLU = 21, /* Ctrl+u */
    LINENOISE_KEY_CTRLW = 23, /* Ctrl+w */
    LINENOISE_KEY_ESC = 27, /* Escape */
    LINENOISE_KEY_BACKSPACE = 127 /* Backspace */
};

struct linenoiseCompletions
{
    size_t len;
    char **cvec;
};

void linenoiseSetCompletionCallback(linenoiseCompletionCallback *);
void linenoiseSetHintsCallback(linenoiseHintsCallback *);
void linenoiseSetFreeHintsCallback(linenoiseFreeHintsCallback *);
void linenoiseAddCompletion(linenoiseCompletions *, const char *);

char *linenoise(const char *prompt);
void linenoiseFree(void *ptr);
int linenoiseHistoryAdd(const char *line);
int linenoiseHistorySetMaxLen(int len);
int linenoiseHistorySave(const char *filename);
int linenoiseHistoryLoad(const char *filename);
void linenoiseClearScreen(void);
void linenoiseSetMultiLine(int ml);
void linenoisePrintKeyCodes(void);
void linenoiseMaskModeEnable(void);
void linenoiseMaskModeDisable(void);

void linenoiseSetEncodingFunctions(linenoisePrevCharLen *prevCharLenFunc, linenoiseNextCharLen *nextCharLenFunc, linenoiseReadCode *readCodeFunc);

