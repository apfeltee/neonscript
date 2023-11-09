
#include "neon.h"

int main(int argc, char** argv)
{
    int i;
    int nargc;
    int exitcode;
    char oc;
    char noc;
    char** nargv;
    char* codeline;
    const char* filename;
    NeonValue unused;
    NeonState* state;
    exitcode = 0;
    nargv = argv;
    nargc = argc;
    codeline = NULL;
    state = neon_state_make();
    
    for(i=1; i<argc; i++)
    {
        if(argv[i][0] == '-')
        {
            oc = argv[i][1];
            noc = argv[i][2];
            if(oc == 'e')
            {
                if(noc == 0)
                {
                    if((i+1) != argc)
                    {
                        codeline = argv[i+1];
                        nargc--;
                        nargv++;
                    }
                    else
                    {
                        fprintf(stderr, "-e needs a value\n");
                        return 1;
                    }
                }
                else
                {
                    codeline = (argv[i] + 2);
                }
                nargc--;
                nargv++;
            }
            else if(oc == 'd')
            {
                state->conf.shouldprintruntime = true;
                nargc--;
                nargv++;
            }
            else if(oc == 's')
            {
                state->conf.strictmode = true;
                nargc--;
                nargv++;
            }
            else
            {
                fprintf(stderr, "invalid flag '-%c'\n", oc);
                return 1;
            }
        }
    }
    if(codeline != NULL)
    {
        //fprintf(stderr, "codeline=%s\n", codeline);
        neon_state_runsource(state, codeline, strlen(codeline), false, &unused);
    }
    else
    {
        if(nargc > 1)
        {
            filename = nargv[1];
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

