---
order: 20
description: A struct whose defaulted operator<=> gives ==, !=, <, <=, > and >= member by member (C++20)
ask: Name = Version
includes: <compare>
---
// The defaulted <=> compares the members in the order they're declared, so {{Name}}s sort and compare
// with every operator. Write your own operator<=> (and operator==) for any other order.
struct {{Name}}
{
    int major = 0;
    int minor = 0;
    int patch = 0;

    auto operator<=>(const {{Name}}&) const = default;
};
