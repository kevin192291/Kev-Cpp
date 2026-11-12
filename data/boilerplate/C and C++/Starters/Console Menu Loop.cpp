---
order: 40
description: A numbered text menu that repeats until Quit and asks again after input that isn't a number
whole-file: yes
file: main.cpp
---
// A console menu that repeats until the user chooses Quit (or input ends).
#include <iostream>
#include <optional>
#include <sstream>
#include <string>

namespace
{
    // Reads a whole line and parses it, so stray text never leaves std::cin in a failed state.
    // Returns std::nullopt only when input has ended.
    std::optional<int> readInt(const std::string& prompt)
    {
        std::string line;
        while (true)
        {
            std::cout << prompt;
            if (!std::getline(std::cin, line))
            {
                return std::nullopt;
            }
            std::istringstream in(line);
            int value = 0;
            char extra = 0;
            if (in >> value && !(in >> extra))
            {
                return value;
            }
            std::cout << "Please enter a whole number.\n";
        }
    }
}

int main()
{
    while (true)
    {
        std::cout << "\n1) Say hello\n2) Add two numbers\n0) Quit\n";
        const std::optional<int> choice = readInt("> ");
        if (!choice || *choice == 0)
        {
            std::cout << "Bye.\n";
            return 0;
        }

        switch (*choice)
        {
        case 1:
            std::cout << "Hello!\n";
            break;
        case 2: {
            const std::optional<int> a = readInt("First number: ");
            const std::optional<int> b = readInt("Second number: ");
            if (a && b)
            {
                std::cout << *a << " + " << *b << " = " << *a + *b << '\n';
            }
            break;
        }
        {{cursor}}
        default:
            std::cout << "There's no option " << *choice << ".\n";
            break;
        }
    }
}
