---
label: Variadic Template (Fold Expression)
order: 50
description: Functions taking any number of arguments of any types, expanded with fold expressions
includes: <iostream>
---
// Args... is a pack of any number of types; a fold expression like (... op pack) expands over it.

// print(1, "two", 3.0) prints "1, two, 3".
template <typename First, typename... Rest>
void print(const First& first, const Rest&... rest)
{
    std::cout << first;
    ((std::cout << ", " << rest), ...);
    std::cout << '\n';
}

// sumAll(1, 2, 3.5) is 6.5; sumAll() is 0.
template <typename... Args>
auto sumAll(const Args&... args)
{
    return (args + ... + 0);
}

// allTrue(a, b, c) is a && b && c.
template <typename... Args>
bool allTrue(const Args&... args)
{
    return (static_cast<bool>(args) && ...);
}
