
#pragma once
/* Optparse --- portable, reentrant, embeddable, getopt-like option parser
 *
 * This is free and unencumbered software released into the public domain.
 *
 * The POSIX getopt() option parser has three fatal flaws. These flaws
 * are solved by Optparse.
 *
 * 1) Parser state is stored entirely in global variables, some of
 * which are static and inaccessible. This means only one thread can
 * use getopt(). It also means it's not possible to recursively parse
 * nested sub-arguments while in the middle of argument parsing.
 * Optparse fixes this by storing all state on a local struct.
 *
 * 2) The POSIX standard provides no way to properly reset the parser.
 * This means for portable code that getopt() is only good for one
 * run, over one argv with one option string. It also means subcommand
 * options cannot be processed with getopt(). Most implementations
 * provide a method to reset the parser, but it's not portable.
 * Optparse provides an optprs_nextpositional() function for stepping over
 * subcommands and continuing parsing of options with another option
 * string. The Optparse struct itself can be passed around to
 * subcommand handlers for additional subcommand option parsing. A
 * full reset can be achieved by with an additional optprs_init().
 *
 * 3) Error messages are printed to stderr. This can be disabled with
 * opterr, but the messages themselves are still inaccessible.
 * Optparse solves this by writing an error message in its errmsg
 * field. The downside to Optparse is that this error message will
 * always be in English rather than the current locale.
 *
 * Optparse should be familiar with anyone accustomed to getopt(), and
 * it could be a nearly drop-in replacement. The option string is the
 * same and the fields have the same names as the getopt() global
 * variables (optarg, optind, optopt).
 *
 * Optparse also supports GNU-style long options with optprs_nextlongflag().
 * The interface is slightly different and simpler than getopt_long().
 *
 * By default, argv is permuted as it is parsed, moving non-option
 * arguments to the end. This can be disabled by setting the `permute`
 * field to 0 after initialization.
 */

#include <stdbool.h>

#define OPTPARSE_MSG_INVALID "invalid option"
#define OPTPARSE_MSG_MISSING "option requires an argument"
#define OPTPARSE_MSG_TOOMANY "option takes no arguments"

enum optargtype_t
{
    OPTPARSE_NONE,
    OPTPARSE_REQUIRED,
    OPTPARSE_OPTIONAL
};

typedef struct optcontext_t optcontext_t;
typedef struct optlongflags_t optlongflags_t;
typedef enum optargtype_t optargtype_t;

struct optcontext_t
{
    char** argv;
    int argc;
    int permute;
    int optind;
    int optopt;
    char* optarg;
    char errmsg[64];
    int subopt;
};

struct optlongflags_t
{
    const char* longname;
    int shortname;
    optargtype_t argtype;
    const char* helptext;
};

/* optparse.h */
int optprs_makeerror(optcontext_t* ox, const char* msg, const char* data);
bool optbits_isdashdash(const char* arg);
bool optbits_isshortopt(const char* arg);
bool optbits_islongopt(const char* arg);
void optbits_permute(optcontext_t* ox, int index);
int optbits_getargtype(const char* optstring, char c);
bool optbits_islongoptsend(const optlongflags_t* longopts, int i);
void optbits_fromlong(const optlongflags_t* longopts, char* optstring);
bool optbits_matchlongopts(const char* longname, const char* option);
char* optbits_getlongoptsarg(char* option);
int optbits_longfallback(optcontext_t* ox, const optlongflags_t* longopts, int* longindex);
void optprs_init(optcontext_t* ox, int argc, char* *argv);
int optprs_nextshortflag(optcontext_t* ox, const char* optstring);
int optprs_nextlongflag(optcontext_t* ox, const optlongflags_t* longopts, int* longindex);
char* optprs_nextpositional(optcontext_t* ox);

