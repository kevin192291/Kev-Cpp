---
order: 20
description: A main() that reads flags, an option with a value and file names, with --help and usage errors
whole-file: yes
file: main.cpp
---
// Reads the command line: -v, -o FILE, --help and any number of input names.
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

int main(int argc, char* argv[])
{
    const std::vector<std::string_view> args(argv + 1, argv + argc);

    bool verbose = false;
    std::string output = "out.txt";
    std::vector<std::string_view> inputs;

    for (std::size_t i = 0; i < args.size(); ++i)
    {
        const std::string_view arg = args[i];
        if (arg == "-h" || arg == "--help")
        {
            std::cout << "Usage: " << argv[0] << " [-v] [-o FILE] INPUT...\n";
            return 0;
        }
        else if (arg == "-v" || arg == "--verbose")
        {
            verbose = true;
        }
        else if (arg == "-o" || arg == "--output")
        {
            if (i + 1 == args.size())
            {
                std::cerr << "error: " << arg << " needs a file name\n";
                return 2;
            }
            output = args[++i];
        }
        else if (arg.starts_with('-'))
        {
            std::cerr << "error: unknown option " << arg << " (try --help)\n";
            return 2;
        }
        else
        {
            inputs.push_back(arg);
        }
    }

    if (inputs.empty())
    {
        std::cerr << "error: no input given (try --help)\n";
        return 2;
    }

    if (verbose)
    {
        std::cout << inputs.size() << " input(s), writing to " << output << '\n';
    }
    for (const std::string_view input : inputs)
    {
        std::cout << input << '\n';
        {{cursor}}
    }
    return 0;
}
