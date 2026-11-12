---
order: 30
description: Reads standard input line by line until it ends (Ctrl+D in a terminal, or the end of a piped file)
whole-file: yes
file: main.cpp
---
// Reads lines from standard input until it ends: type them and press Ctrl+D, or run it as ./app < file.txt
#include <iostream>
#include <string>

int main()
{
    std::string line;
    int lineCount = 0;
    // For whitespace-separated values instead of lines: while (std::cin >> value)
    while (std::getline(std::cin, line))
    {
        ++lineCount;
        std::cout << lineCount << ": " << line << '\n';
        {{cursor}}
    }
    std::cout << lineCount << " lines\n";
    return 0;
}
