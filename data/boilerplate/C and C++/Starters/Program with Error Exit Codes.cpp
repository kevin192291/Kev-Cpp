---
order: 50
description: main() turns exceptions into messages and exit codes: 0 success, 1 failure, 2 bad command line
whole-file: yes
file: main.cpp
---
// main() catches every exception and turns it into a message and an exit code that scripts can test ($?).
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>

namespace
{
    enum ExitCode
    {
        ExitOk = 0,
        ExitFailure = 1, // something went wrong while running
        ExitUsage = 2,   // bad command line, as most Unix tools do
    };

    class UsageError : public std::runtime_error
    {
    public:
        using std::runtime_error::runtime_error;
    };

    int run(int argc, char* argv[])
    {
        if (argc != 2)
        {
            throw UsageError("expected one file name");
        }
        const std::string fileName = argv[1];
        std::cout << "Working on " << fileName << '\n';
        {{cursor}}
        return ExitOk;
    }
}

int main(int argc, char* argv[])
{
    try
    {
        return run(argc, argv);
    }
    catch (const UsageError& e)
    {
        std::cerr << "error: " << e.what() << "\nUsage: " << argv[0] << " FILE\n";
        return ExitUsage;
    }
    catch (const std::exception& e)
    {
        std::cerr << "error: " << e.what() << '\n';
        return ExitFailure;
    }
    catch (...)
    {
        std::cerr << "error: unknown exception\n";
        return ExitFailure;
    }
}
