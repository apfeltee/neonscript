
#include "neon.h"

void nn_strformat_init(NNState* state, NNFormatInfo* nfi, NNIOStream* writer, const char* fmtstr, size_t fmtlen)
{
    nfi->pstate = state;
    nfi->fmtstr = fmtstr;
    nfi->fmtlen = fmtlen;
    nfi->writer = writer;
}

void nn_strformat_destroy(NNFormatInfo* nfi)
{
    (void)nfi;
}

bool nn_strformat_format(NNFormatInfo* nfi, int argc, int argbegin, NNValue* argv)
{
    int ch;
    int ival;
    int nextch;
    bool ok;
    size_t i;
    size_t argpos;
    NNValue cval;
    i = 0;
    argpos = argbegin;
    ok = true;
    while(i < nfi->fmtlen)
    {
        ch = nfi->fmtstr[i];
        nextch = -1;
        if((i + 1) < nfi->fmtlen)
        {
            nextch = nfi->fmtstr[i+1];
        }
        i++;
        if(ch == '%')
        {
            if(nextch == '%')
            {
                nn_iostream_writechar(nfi->writer, '%');
            }
            else
            {
                i++;
                if((int)argpos > argc)
                {
                    nn_except_throwclass(nfi->pstate, nfi->pstate->exceptions.argumenterror, "too few arguments");
                    ok = false;
                    cval = nn_value_makenull();
                }
                else
                {
                    cval = argv[argpos];
                }
                argpos++;
                switch(nextch)
                {
                    case 'q':
                    case 'p':
                        {
                            nn_iostream_printvalue(nfi->writer, cval, true, true);
                        }
                        break;
                    case 'c':
                        {
                            ival = (int)nn_value_asnumber(cval);
                            nn_iostream_printf(nfi->writer, "%c", ival);
                        }
                        break;
                    /* TODO: implement actual field formatting */
                    case 's':
                    case 'd':
                    case 'i':
                    case 'g':
                        {
                            nn_iostream_printvalue(nfi->writer, cval, false, true);
                        }
                        break;
                    default:
                        {
                            nn_except_throwclass(nfi->pstate, nfi->pstate->exceptions.argumenterror, "unknown/invalid format flag '%%c'", nextch);
                        }
                        break;
                }
            }
        }
        else
        {
            nn_iostream_writechar(nfi->writer, ch);
        }
    }
    return ok;
}
