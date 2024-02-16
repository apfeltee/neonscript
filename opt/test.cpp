
#include <iostream>
#include <fstream>
#include "../optionparser.h"

struct options
{
    int verbosity = 0;
    neon::Util::StrBuffer outfile = "a.out";
};

int main(int argc, char* argv[])
{
    size_t i;
    options opts;
    neon::Util::OptionParser prs;
    try
    {
        prs.onUnknownOption([&](const auto& v)
        {
            fprintf(stderr, "unknown option '%s'!\n", v);
            return false;
        });
        prs.on({"-v", "--verbose"}, "increase verbosity (try passing '-v' several times!)", [&]
        {
            opts.verbosity++;
            printf("** verbosity is now %d\n", opts.verbosity);
        });
        prs.on({"-d", "--debug", "--toggledebug"}, "toggle debug mode", [&]
        {
            printf("** toggling debug mode\n");
        });
        prs.on({"-o<file>", "--outputfile=<file>"}, "set outputfile", [&](const auto& v)
        {
            auto s = v.str();
            printf("** outfile = '%s'\n", s.data());
            opts.outfile = v.str();
        });
        prs.on({"-n<n>", "--number=<n>"}, "pass a value", [&](const neon::Util::OptionParser::Value& v)
        {
            auto s = v.str();
            printf("** value is %s\n", s.data());
        });
        prs.on({"-I<path>", "-A<path>", "--include=<path>"}, "add a path to include searchpath", [&](const auto& v)
        {
            auto s = v.str();
            printf("** include: '%s'\n", s.data());
        });
        try
        {
            prs.parse(argc, argv);
            auto pos = prs.positional();
            if((pos.size() == 0) && (argc == 1))
            {
                prs.help(std::cout);
                return 1;
            }
            else
            {
                printf("** positional:\n");
                for(i=0; i<pos.size(); i++)
                {
                    auto s = pos[i];
                    printf("  [%d] %.*s\n", i, (int)s.length(), s.data());
                }
            }
        }
        catch(std::runtime_error& ex)
        {
            fprintf(stderr, "parse error: %s\n", ex.what());
        }
    }
    catch(std::runtime_error& ex)
    {
        fprintf(stderr, "setting error: %s\n", ex.what());
    }
    return 0;
}
