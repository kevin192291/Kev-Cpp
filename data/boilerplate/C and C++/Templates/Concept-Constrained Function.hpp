---
order: 30
description: Templates restricted with standard concepts (C++20), in the three ways C++ allows writing it
includes: <concepts> <ranges>
---
// A concept states what a template needs, so a wrong argument fails at the call with a short message.

// A requires clause, for conditions on derived types: any range of numbers.
template <std::ranges::input_range R>
    requires std::integral<std::ranges::range_value_t<R>> || std::floating_point<std::ranges::range_value_t<R>>
auto sum(const R& values)
{
    std::ranges::range_value_t<R> total{};
    for (const auto& value : values)
    {
        total += value;
    }
    return total;
}

// A concept in place of typename.
template <std::floating_point T>
T half(T value)
{
    return value / 2;
}

// A concept before auto in the parameter list (an abbreviated template).
auto squared(std::integral auto value)
{
    return value * value;
}
