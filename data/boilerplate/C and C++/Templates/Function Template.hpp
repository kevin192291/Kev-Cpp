---
order: 10
description: A function template that prints any container whose elements work with <<
includes: <iostream> <string_view>
---
// printAll(values) prints a vector, list, set, array... of anything std::cout can print; the compiler
// writes one copy of the function for each container type it's used with.
template <typename Container>
void printAll(const Container& items, std::string_view separator = ", ")
{
    bool first = true;
    for (const auto& item : items)
    {
        if (!first)
        {
            std::cout << separator;
        }
        std::cout << item;
        first = false;
    }
    std::cout << '\n';
}
