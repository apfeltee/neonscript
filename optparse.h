
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
 * Optparse provides an nextPositional() function for stepping over
 * subcommands and continuing parsing of options with another option
 * string. The Optparse struct itself can be passed around to
 * subcommand handlers for additional subcommand option parsing. A
 * full reset can be achieved by with an additional OptionParser() constructor call.
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
 * Optparse also supports GNU-style long options with nextLong().
 * The interface is slightly different and simpler than getopt_long().
 *
 * By default, argv is permuted as it is parsed, moving non-option
 * arguments to the end. This can be disabled by setting the `dopermute`
 * field to 0 after initialization.
 */

#include <stdbool.h>
#include <stdlib.h>

struct OptionParser
{
    public:
        enum ArgType
        {
            A_NONE,
            A_REQUIRED,
            A_OPTIONAL
        };

        struct LongFlags
        {
            const char* longname;
            int shortname;
            ArgType argtype;
            const char* helptext;
        };


    public:
        bool isDashDash(const char* arg)
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

        bool isShortOpt(const char* arg)
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

        static bool isLongOpt(const char* arg)
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

        static int getArgType(const char* optstring, char c)
        {
            int count;
            count = A_NONE;
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

        static bool isLongOptsEnd(const LongFlags* longopts, int i)
        {
            if(!longopts[i].longname && !longopts[i].shortname)
            {
                return true;
            }
            return false;
        }

        void fromLong(const LongFlags* longopts, char* optstring)
        {
            int i;
            int a;
            char* p;
            p = optstring;
            for(i = 0; !isLongOptsEnd(longopts, i); i++)
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
        static bool matchLongOpts(const char* longname, const char* option)
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
        static char* getLongOptsArg(char* option)
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

    public:
        char** argv;
        int argc;
        int dopermute;
        int optind;
        int optopt;
        char* optarg;
        char errmsg[64];
        int subopt;

    public:
        /**
         * Initializes the parser state.
         */
        OptionParser(int ac, char** av)
        {
            this->argv = av;
            this->argc = ac;
            this->dopermute = 1;
            this->optind = argv[0] != 0;
            this->subopt = 0;
            this->optarg = 0;
            this->errmsg[0] = '\0';
        }

        int makeError(const char* msg, const char* data)
        {
            unsigned p;
            const char* sep;
            p = 0;
            sep = " -- '";
            while(*msg)
            {
                this->errmsg[p++] = *msg++;
            }
            while(*sep)
            {
                this->errmsg[p++] = *sep++;
            }
            while(p < sizeof(this->errmsg) - 2 && *data)
            {
                this->errmsg[p++] = *data++;
            }
            this->errmsg[p++] = '\'';
            this->errmsg[p++] = '\0';
            return '?';
        }

        void permute(int index)
        {
            int i;
            char* nonoption;
            nonoption = this->argv[index];
            for(i = index; i < this->optind - 1; i++)
            {
                this->argv[i] = this->argv[i + 1];
            }
            this->argv[this->optind - 1] = nonoption;
        }

        /**
         * Handles GNU-style long options in addition to getopt() options.
         * This works a lot like GNU's getopt_long(). The last option in
         * longopts must be all zeros, marking the end of the array. The
         * longindex argument may be NULL.
         */
        int nextLong(const LongFlags* longopts, int* longindex)
        {
            int i;
            int r;
            int index;
            char* arg;
            char* option;
            const char* name;
            option = this->argv[this->optind];
            if(option == 0)
            {
                return -1;
            }
            else if(isDashDash(option))
            {
                this->optind++; /* consume "--" */
                return -1;
            }
            else if(isShortOpt(option))
            {
                return this->longFallback(longopts, longindex);
            }
            else if(!isLongOpt(option))
            {
                if(this->dopermute)
                {
                    index = this->optind++;
                    r = this->nextLong(longopts, longindex);
                    this->permute(index);
                    this->optind--;
                    return r;
                }
                else
                {
                    return -1;
                }
            }
            /* Parse as long option. */
            this->errmsg[0] = '\0';
            this->optopt = 0;
            this->optarg = 0;
            option += 2; /* skip "--" */
            this->optind++;
            for(i = 0; !isLongOptsEnd(longopts, i); i++)
            {
                name = longopts[i].longname;
                if(matchLongOpts(name, option))
                {
                    if(longindex)
                    {
                        *longindex = i;
                    }
                    this->optopt = longopts[i].shortname;
                    arg = getLongOptsArg(option);
                    if(longopts[i].argtype == A_NONE && arg != 0)
                    {
                        return this->makeError("option takes no arguments", name);
                    }
                    if(arg != 0)
                    {
                        this->optarg = arg;
                    }
                    else if(longopts[i].argtype == A_REQUIRED)
                    {
                        this->optarg = this->argv[this->optind];
                        if(this->optarg == 0)
                        {
                            return this->makeError("option requires an argument", name);
                        }
                        else
                        {
                            this->optind++;
                        }
                    }
                    return this->optopt;
                }
            }
            return this->makeError("invalid option", option);
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
        int nextShort(const char* optstring)
        {
            int r;
            int type;
            int index;
            char* next;
            char* option;
            char str[2] = { 0, 0 };
            option = this->argv[this->optind];
            this->errmsg[0] = '\0';
            this->optopt = 0;
            this->optarg = 0;
            if(option == 0)
            {
                return -1;
            }
            else if(isDashDash(option))
            {
                /* consume "--" */
                this->optind++;
                return -1;
            }
            else if(!isShortOpt(option))
            {
                if(this->dopermute)
                {
                    index = this->optind++;
                    r = this->nextShort(optstring);
                    this->permute(index);
                    this->optind--;
                    return r;
                }
                else
                {
                    return -1;
                }
            }
            option += this->subopt + 1;
            this->optopt = option[0];
            type = getArgType(optstring, option[0]);
            next = this->argv[this->optind + 1];
            switch(type)
            {
                case -1:
                    {
                        str[1] = 0;
                        str[0] = option[0];
                        this->optind++;
                        return this->makeError("invalid option", str);
                    }
                    break;
                case A_NONE:
                    {
                        if(option[1])
                        {
                            this->subopt++;
                        }
                        else
                        {
                            this->subopt = 0;
                            this->optind++;
                        }
                        return option[0];
                    }
                    break;
                case A_REQUIRED:
                    {
                        this->subopt = 0;
                        this->optind++;
                        if(option[1])
                        {
                            this->optarg = option + 1;
                        }
                        else if(next != 0)
                        {
                            this->optarg = next;
                            this->optind++;
                        }
                        else
                        {
                            str[1] = 0;
                            str[0] = option[0];
                            this->optarg = 0;
                            return this->makeError("option requires an argument", str);
                        }
                        return option[0];
                    }
                    break;
                case A_OPTIONAL:
                    {
                        this->subopt = 0;
                        this->optind++;
                        if(option[1])
                        {
                            this->optarg = option + 1;
                        }
                        else
                        {
                            this->optarg = 0;
                        }
                        return option[0];
                    }
                    break;
            }
            return 0;
        }

        int longFallback(const LongFlags* longopts, int* longindex)
        {
            int i;
            int result;
            /* 96 ASCII printable characters */
            char optstring[96 * 3 + 1];
            fromLong(longopts, optstring);
            result = this->nextShort(optstring);
            if(longindex != 0)
            {
                *longindex = -1;
                if(result != -1)
                {
                    for(i = 0; !isLongOptsEnd(longopts, i); i++)
                    {
                        if(longopts[i].shortname == this->optopt)
                        {
                            *longindex = i;
                        }
                    }
                }
            }
            return result;
        }



        /**
         * Used for stepping over non-option arguments.
         * @return the next non-option argument, or NULL for no more arguments
         *
         * Argument parsing can continue with optparse() after using this
         * function. That would be used to parse the options for the
         * subcommand returned by nextPositional(). This function allows you to
         * ignore the value of optind.
         */
        char* nextPositional()
        {
            char* option;
            option = this->argv[this->optind];
            this->subopt = 0;
            if(option != 0)
            {
                this->optind++;
            }
            return option;
        }


};



