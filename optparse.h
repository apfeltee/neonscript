
#if !defined(__NNOPTPARSEHEADERFILE_H__)
#define __NNOPTPARSEHEADERFILE_H__
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

static int optprs_nextshortflag(optcontext_t* ox, const char* optstring);

static int optprs_makeerror(optcontext_t* ox, const char* msg, const char* data)
{
    unsigned p;
    const char* sep;
    p = 0;
    sep = " -- '";
    while(*msg)
    {
        ox->errmsg[p++] = *msg++;
    }
    while(*sep)
    {
        ox->errmsg[p++] = *sep++;
    }
    while(p < sizeof(ox->errmsg) - 2 && *data)
    {
        ox->errmsg[p++] = *data++;
    }
    ox->errmsg[p++] = '\'';
    ox->errmsg[p++] = '\0';
    return '?';
}

static bool optbits_isdashdash(const char* arg)
{
    if(arg != NULL)
    {
        if((arg[0] == '-') && (arg[1] == '-') && (arg[2] == '\0'))
        {
            return true;
        }
    }
    return false;
}

static bool optbits_isshortopt(const char* arg)
{
    if(arg != NULL)
    {
        if((arg[0] == '-') && (arg[1] != '-') && (arg[1] != '\0'))
        {
            return true;
        }
    }
    return false;
}

static bool optbits_islongopt(const char* arg)
{
    if(arg != NULL)
    {
        if((arg[0] == '-') && (arg[1] == '-') && (arg[2] != '\0'))
        {
            return true;
        }
    }
    return false;
}

static void optbits_permute(optcontext_t* ox, int index)
{
    int i;
    char* nonoption;
    nonoption = ox->argv[index];
    for(i = index; i < ox->optind - 1; i++)
    {
        ox->argv[i] = ox->argv[i + 1];
    }
    ox->argv[ox->optind - 1] = nonoption;
}

static int optbits_getargtype(const char* optstring, char c)
{
    int count;
    count = OPTPARSE_NONE;
    if(c == ':')
    {
        return -1;
    }
    for(; *optstring && c != *optstring; optstring++)
    {
    }
    if(!*optstring)
    {
        return -1;
    }
    if(optstring[1] == ':')
    {
        count += optstring[2] == ':' ? 2 : 1;
    }
    return count;
}

static bool optbits_islongoptsend(const optlongflags_t* longopts, int i)
{
    if(!longopts[i].longname && !longopts[i].shortname)
    {
        return true;
    }
    return false;
}

static void optbits_fromlong(const optlongflags_t* longopts, char* optstring)
{
    int i;
    int a;
    char* p;
    p = optstring;
    for(i = 0; !optbits_islongoptsend(longopts, i); i++)
    {
        if(longopts[i].shortname && longopts[i].shortname < 127)
        {
            *p++ = longopts[i].shortname;
            for(a = 0; a < (int)longopts[i].argtype; a++)
            {
                *p++ = ':';
            }
        }
    }
    *p = '\0';
}

/* Unlike strcmp(), handles options containing "=". */
static bool optbits_matchlongopts(const char* longname, const char* option)
{
    const char *a;
    const char* n;
    a = option;
    n = longname;
    if(longname == 0)
    {
        return 0;
    }
    for(; *a && *n && *a != '='; a++, n++)
    {
        if(*a != *n)
        {
            return 0;
        }
    }
    return *n == '\0' && (*a == '\0' || *a == '=');
}

/* Return the part after "=", or NULL. */
static char* optbits_getlongoptsarg(char* option)
{
    for(; *option && *option != '='; option++)
    {
    }
    if(*option == '=')
    {
        return option + 1;
    }
    return NULL;
}

static int optbits_longfallback(optcontext_t* ox, const optlongflags_t* longopts, int* longindex)
{
    int i;
    int result;
    /* 96 ASCII printable characters */
    char optstring[96 * 3 + 1];
    optbits_fromlong(longopts, optstring);
    result = optprs_nextshortflag(ox, optstring);
    if(longindex != 0)
    {
        *longindex = -1;
        if(result != -1)
        {
            for(i = 0; !optbits_islongoptsend(longopts, i); i++)
            {
                if(longopts[i].shortname == ox->optopt)
                {
                    *longindex = i;
                }
            }
        }
    }
    return result;
}

/**
 * Initializes the parser state.
 */
static void optprs_init(optcontext_t* ox, int argc, char** argv)
{
    ox->argv = argv;
    ox->argc = argc;
    ox->permute = 1;
    ox->optind = argv[0] != 0;
    ox->subopt = 0;
    ox->optarg = 0;
    ox->errmsg[0] = '\0';
}

/**
 * Read the next option in the argv array.
 * @param optstring a getopt()-formatted option string.
 * @return the next option character, -1 for done, or '?' for error
 *
 * Just like getopt(), a character followed by no colons means no
 * argument. One colon means the option has a required argument. Two
 * colons means the option takes an optional argument.
 */
static int optprs_nextshortflag(optcontext_t* ox, const char* optstring)
{
    int r;
    int type;
    int index;
    char* next;
    char* option;
    char str[2] = { 0, 0 };
    option = ox->argv[ox->optind];
    ox->errmsg[0] = '\0';
    ox->optopt = 0;
    ox->optarg = 0;
    if(option == 0)
    {
        return -1;
    }
    else if(optbits_isdashdash(option))
    {
        /* consume "--" */
        ox->optind++;
        return -1;
    }
    else if(!optbits_isshortopt(option))
    {
        if(ox->permute)
        {
            index = ox->optind++;
            r = optprs_nextshortflag(ox, optstring);
            optbits_permute(ox, index);
            ox->optind--;
            return r;
        }
        else
        {
            return -1;
        }
    }
    option += ox->subopt + 1;
    ox->optopt = option[0];
    type = optbits_getargtype(optstring, option[0]);
    next = ox->argv[ox->optind + 1];
    switch(type)
    {
        case -1:
            {
                str[1] = 0;
                str[0] = option[0];
                ox->optind++;
                return optprs_makeerror(ox, OPTPARSE_MSG_INVALID, str);
            }
            break;
        case OPTPARSE_NONE:
            {
                if(option[1])
                {
                    ox->subopt++;
                }
                else
                {
                    ox->subopt = 0;
                    ox->optind++;
                }
                return option[0];
            }
            break;
        case OPTPARSE_REQUIRED:
            {
                ox->subopt = 0;
                ox->optind++;
                if(option[1])
                {
                    ox->optarg = option + 1;
                }
                else if(next != 0)
                {
                    ox->optarg = next;
                    ox->optind++;
                }
                else
                {
                    str[1] = 0;
                    str[0] = option[0];
                    ox->optarg = 0;
                    return optprs_makeerror(ox, OPTPARSE_MSG_MISSING, str);
                }
                return option[0];
            }
            break;
        case OPTPARSE_OPTIONAL:
            {
                ox->subopt = 0;
                ox->optind++;
                if(option[1])
                {
                    ox->optarg = option + 1;
                }
                else
                {
                    ox->optarg = 0;
                }
                return option[0];
            }
            break;
    }
    return 0;
}

/**
 * Handles GNU-style long options in addition to getopt() options.
 * This works a lot like GNU's getopt_long(). The last option in
 * longopts must be all zeros, marking the end of the array. The
 * longindex argument may be NULL.
 */
static int optprs_nextlongflag(optcontext_t* ox, const optlongflags_t* longopts, int* longindex)
{
    int i;
    int r;
    int index;
    char* arg;
    char* option;
    const char* name;
    option = ox->argv[ox->optind];
    if(option == 0)
    {
        return -1;
    }
    else if(optbits_isdashdash(option))
    {
        ox->optind++; /* consume "--" */
        return -1;
    }
    else if(optbits_isshortopt(option))
    {
        return optbits_longfallback(ox, longopts, longindex);
    }
    else if(!optbits_islongopt(option))
    {
        if(ox->permute)
        {
            index = ox->optind++;
            r = optprs_nextlongflag(ox, longopts, longindex);
            optbits_permute(ox, index);
            ox->optind--;
            return r;
        }
        else
        {
            return -1;
        }
    }
    /* Parse as long option. */
    ox->errmsg[0] = '\0';
    ox->optopt = 0;
    ox->optarg = 0;
    option += 2; /* skip "--" */
    ox->optind++;
    for(i = 0; !optbits_islongoptsend(longopts, i); i++)
    {
        name = longopts[i].longname;
        if(optbits_matchlongopts(name, option))
        {
            if(longindex)
            {
                *longindex = i;
            }
            ox->optopt = longopts[i].shortname;
            arg = optbits_getlongoptsarg(option);
            if(longopts[i].argtype == OPTPARSE_NONE && arg != 0)
            {
                return optprs_makeerror(ox, OPTPARSE_MSG_TOOMANY, name);
            }
            if(arg != 0)
            {
                ox->optarg = arg;
            }
            else if(longopts[i].argtype == OPTPARSE_REQUIRED)
            {
                ox->optarg = ox->argv[ox->optind];
                if(ox->optarg == 0)
                {
                    return optprs_makeerror(ox, OPTPARSE_MSG_MISSING, name);
                }
                else
                {
                    ox->optind++;
                }
            }
            return ox->optopt;
        }
    }
    return optprs_makeerror(ox, OPTPARSE_MSG_INVALID, option);
}

/**
 * Used for stepping over non-option arguments.
 * @return the next non-option argument, or NULL for no more arguments
 *
 * Argument parsing can continue with optparse() after using this
 * function. That would be used to parse the options for the
 * subcommand returned by optprs_nextpositional(). This function allows you to
 * ignore the value of optind.
 */
static char* optprs_nextpositional(optcontext_t* ox)
{
    char* option;
    option = ox->argv[ox->optind];
    ox->subopt = 0;
    if(option != 0)
    {
        ox->optind++;
    }
    return option;
}

#endif
