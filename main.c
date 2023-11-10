
#include "neon.h"
#include "optparse.h"

int main(int argc, char** argv)
{
    int i;
    int exitcode;
    char* codeline;
    const char* filename;
    NeonValue unused;
    NeonState* state;
    exitcode = 0;
    codeline = NULL;
    state = neon_state_make();

    char *arg;
    int opt, longindex;
    optcontext_t options;
    optlongflags_t longopts[] =
    {
        {"strict", 's', OPTPARSE_NONE},
        {"debug", 'd', OPTPARSE_NONE},
        {"eval", 'e', OPTPARSE_REQUIRED},
        {0, 0, 0}
    };
    int nsz;
    int nargc;
    char** nargv;
    nargc = 0;
    optprs_init(&options, argc, argv);
    options.permute = 0;
    while ((opt = optprs_nextlongflag(&options, longopts, &longindex)) != -1)
    {
        if(opt == '?')
        {
            printf("%s: %s\n", argv[0], options.errmsg);
        }
        else if(longopts[longindex].shortname == 'd')
        {
            state->conf.shouldprintruntime = true;            
        }
        else if(longopts[longindex].shortname == 's')
        {
            state->conf.strictmode = true;            
        }
        else if(longopts[longindex].shortname == 'e')
        {
            codeline = options.optarg;           
        }
    }
    nsz = (sizeof(char*) * (options.optind + 1));
    nargv = (char**)malloc(nsz);
    while ((arg = optprs_nextpositional(&options)))
    {
        nargv[nargc] = arg;
        nargc++;
    }
    {
        NeonObjString* os;
        NeonObjArray* oargv;
        oargv = neon_array_make(state);
        for(i=0; i<nargc; i++)
        {
            os = neon_string_copycstr(state, nargv[i]);
            neon_array_push(oargv, neon_value_fromobject(os));
        }
        neon_state_defvalue(state, "ARGV", neon_value_fromobject(oargv));
    }
    if(codeline != NULL)
    {
        //fprintf(stderr, "codeline=%s\n", codeline);
        neon_state_runsource(state, codeline, strlen(codeline), false, &unused);
    }
    else
    {
        if(nargc > 0)
        {
            filename = nargv[0];
            //fprintf(stderr, "filename=%s\n", filename);
            if(!neon_state_runfile(state, filename))
            {
                exitcode = 1;
            }
        }
        else
        {
            repl(state);
        }
    }
    neon_state_destroy(state);
    return exitcode;
}

